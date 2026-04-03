/*
 * Copyright (c) 2010-2026 OTClient <https://github.com/edubart/otclient>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "creature.h"

#include "animator.h"
#include "attachedeffect.h"
#include "game.h"
#include "gameconfig.h"
#include "lightview.h"
#include "localplayer.h"
#include "luavaluecasts_client.h"
#include "map.h"
#include "framework/graphics/texturemanager.h"
#include "protocolcodes.h"
#include "statictext.h"
#include "thingtype.h"
#include "thingtypemanager.h"
#include "tile.h"
#include "paperdoll.h"
#include "framework/core/clock.h"
#include "framework/core/eventdispatcher.h"
#include "framework/core/scheduledevent.h"
#include "framework/graphics/drawpoolmanager.h"
#include "framework/graphics/painter.h"
#include "framework/graphics/shadermanager.h"
#include "framework/ui/uiwidget.h"
#include <framework/core/graphicalapplication.h>
#include <framework/util/stats.h>

double Creature::speedA = 0;
double Creature::speedB = 0;
double Creature::speedC = 0;

Creature::Creature() :m_type(Proto::CreatureTypeUnknown)
{
    g_stats.addCreature();
    m_name.setFont(g_gameConfig.getCreatureNameFont());
    m_name.setAlign(Fw::AlignTopCenter);
    m_typingIconTexture = g_textures.getTexture(g_gameConfig.getTypingIcon());
}

Creature::~Creature() {
    setWidgetInformation(nullptr);
    g_stats.removeCreature();
}

void Creature::onCreate() {
    callLuaField("onCreate");
}

void Creature::draw(const Point& dest, const bool drawThings, LightView* /*lightView*/)
{
    if (!canBeSeen() || !canDraw() || isDead())
        return;

    // Stop walk animation if no sub-tile updates received for a while
    // but NOT during prediction or transition
    if (m_subTileMoving && !m_isPredicting && !m_subTileTransitioning && m_subTileMoveTimer.ticksElapsed() > 100) {
        m_subTileMoving = false;
        m_walkAnimationPhase = 0;
    }

    // Complete sub-tile transition when duration elapsed
    if (m_subTileTransitioning && m_subTileTransitionTimer.ticksElapsed() >= m_subTileTransitionDuration) {
        m_subTileTransitioning = false;
        m_subTileMoving = false;
        m_subTileX = 128;
        m_subTileY = 128;
        m_walkAnimationPhase = 0;
    }

    // Drive walk animation every frame during movement
    if (m_subTileMoving || m_isPredicting || m_subTileTransitioning) {
        updateWalkAnimation();
    }

    if (drawThings) {
        const auto subTileOff = getSubTileOffset();
        if (m_showTimedSquare) {
            g_drawPool.addBoundingRect(Rect(dest + (subTileOff - getDisplacement() + 2) * g_drawPool.getScaleFactor(), Size(28 * g_drawPool.getScaleFactor())), m_timedSquareColor, std::max<int>(static_cast<int>(2 * g_drawPool.getScaleFactor()), 1));
        }

        if (m_showStaticSquare) {
            g_drawPool.addBoundingRect(Rect(dest + (subTileOff - getDisplacement()) * g_drawPool.getScaleFactor(), Size(g_gameConfig.getSpriteSize() * g_drawPool.getScaleFactor())), m_staticSquareColor, std::max<int>(static_cast<int>(2 * g_drawPool.getScaleFactor()), 1));
        }

        auto _dest = dest + getSubTileOffset() * g_drawPool.getScaleFactor();

        auto oldScaleFactor = g_drawPool.getScaleFactor();

        g_drawPool.setScaleFactor(getScaleFactor() + (oldScaleFactor - 1.f));

        if (oldScaleFactor != g_drawPool.getScaleFactor()) {
            _dest -= ((Point(g_gameConfig.getSpriteSize()) + getDisplacement()) / 2) * (g_drawPool.getScaleFactor() - oldScaleFactor);
        }

        internalDraw(_dest);

        if (isMarked())
            internalDraw(_dest, getMarkedColor());
        else if (isHighlighted())
            internalDraw(_dest, getHighlightColor());

        g_drawPool.setScaleFactor(oldScaleFactor);
    }

    // drawLight(dest, lightView);
}

void Creature::drawLight(const Point& dest, LightView* lightView) {
    if (!lightView) return;

    auto light = getLight();

    if (isLocalPlayer() && (g_map.getLight().intensity < 64 || m_position.z > g_gameConfig.getMapSeaFloor())) {
        if (light.intensity == 0) {
            light.intensity = 2;
        } else if (light.color == 0 || light.color > 215) {
            light.color = 215;
        }
    }

    if (light.intensity > 0) {
        lightView->addLightSource(dest + (getSubTileOffset() + (Point(g_gameConfig.getSpriteSize() / 2))) * g_drawPool.getScaleFactor(), light);
    }

    drawAttachedLightEffect(dest + getSubTileOffset() * g_drawPool.getScaleFactor(), lightView);

    for (const auto& paperdoll : m_paperdolls)
        paperdoll->drawLight(dest, m_outfit.hasMount(), lightView);
}

void Creature::draw(const Rect& destRect, const uint8_t size, const bool center)
{
    if (!canDraw())
        return;

    const int baseSprite = g_gameConfig.getSpriteSize();
    const int nativeSize = g_gameConfig.isUseCropSizeForUIDraw()
        ? getExactSize(0, 0, 0)
        : std::max<int>(getRealSize(), getExactSize());
    const int tileCount = 2;
    const int fbSize = tileCount * baseSprite;

    g_drawPool.bindFrameBuffer(fbSize); {
        Point p = center
            ? Point((fbSize - nativeSize) / 2 + (nativeSize - baseSprite)) + getDisplacement()
            : Point(fbSize - baseSprite) + getDisplacement();

        internalDraw(p);
        if (isMarked())           internalDraw(p, getMarkedColor());
        else if (isHighlighted()) internalDraw(p, getHighlightColor());
    }

    Rect out = destRect;
    if (size > 0) out = Rect(destRect.topLeft(), Size(size, size));
    g_drawPool.releaseFrameBuffer(out);
}

void Creature::drawInformation(const MapPosInfo& mapRect, const Point& dest, const int drawFlags)
{
    static constexpr Color
        DEFAULT_COLOR(96, 96, 96),
        NPC_COLOR(0x66, 0xcc, 0xff);

    if (isDead() || !canBeSeen() || !(drawFlags & Otc::DrawCreatureInfo) || !mapRect.isInRange(getPosition()))
        return;

    if (g_gameConfig.isDrawingInformationByWidget()) {
        if (m_widgetInformation)
            m_widgetInformation->draw(mapRect.rect, DrawPoolType::FOREGROUND);
        return;
    }

    const auto displacementX = g_game.getFeature(Otc::GameNegativeOffset) ? 0 : getDisplacementX();
    const auto displacementY = g_game.getFeature(Otc::GameNegativeOffset) ? 0 : getDisplacementY();

    const auto& parentRect = mapRect.rect;
    const auto& creatureOffset = Point(16 - displacementX, -displacementY - 2) + getDrawOffset();

    Point p = dest - mapRect.drawOffset;
    p += (creatureOffset - Point(std::round(m_jumpOffset.x), std::round(m_jumpOffset.y))) * mapRect.scaleFactor;
    p.x *= mapRect.horizontalStretchFactor;
    p.y *= mapRect.verticalStretchFactor;
    p += parentRect.topLeft();

    auto fillColor = DEFAULT_COLOR;

    if (!isCovered()) {
        if (g_game.getFeature(Otc::GameBlueNpcNameColor) && isNpc() && isFullHealth())
            fillColor = NPC_COLOR;
        else fillColor = m_informationColor;
    }

    // calculate main rects

    const auto& nameSize = m_name.getTextSize();
    const int cropSizeText = g_gameConfig.isAdjustCreatureInformationBasedCropSize() ? getExactSize() : 12;
    const int cropSizeBackGround = g_gameConfig.isAdjustCreatureInformationBasedCropSize() ? cropSizeText - nameSize.height() : 0;

    const bool isScaled = g_app.getCreatureInformationScale() != DEFAULT_DISPLAY_DENSITY;
    if (isScaled) {
        p.scale(g_app.getCreatureInformationScale());
    }

    auto backgroundRect = Rect(p.x - (15.5), p.y - cropSizeBackGround, 31, 4);
    auto textRect = Rect(p.x - nameSize.width() / 2.0, p.y - cropSizeText, nameSize);

    constexpr int minNameBarSpacing = 2;
    const int currentSpacing = backgroundRect.top() - textRect.bottom();
    if (currentSpacing < minNameBarSpacing) {
        backgroundRect.moveTop(textRect.bottom() + minNameBarSpacing);
    }

    if (!isScaled) {
        backgroundRect.bind(parentRect);
        textRect.bind(parentRect);
    }

    // distance them
    uint8_t offset = 12 * mapRect.scaleFactor;
    if (isLocalPlayer()) {
        offset *= 2 * mapRect.scaleFactor;
    }

    if (textRect.top() == parentRect.top())
        backgroundRect.moveTop(textRect.top() + offset);
    if (backgroundRect.bottom() == parentRect.bottom())
        textRect.moveTop(backgroundRect.top() - offset);

    // health rect is based on background rect, so no worries
    Rect healthRect = backgroundRect.expanded(-1);
    healthRect.setWidth((m_healthPercent / 100.0) * 29);

    Rect barsRect = backgroundRect;

    if ((drawFlags & Otc::DrawBars) && (g_game.getClientVersion() >= 1100 ? !isNpc() : true)) {
        g_drawPool.addFilledRect(backgroundRect, Color::black);
        g_drawPool.addFilledRect(healthRect, fillColor);

        if (drawFlags & Otc::DrawManaBar && isLocalPlayer()) {
            if (const auto& player = g_game.getLocalPlayer()) {
                if (player->isMage() && player->getMaxManaShield() > 0) {
                    barsRect.moveTop(barsRect.bottom());
                    g_drawPool.addFilledRect(barsRect, Color::black);

                    Rect manaShieldRect = barsRect.expanded(-1);
                    const double maxManaShield = player->getMaxManaShield();
                    manaShieldRect.setWidth((maxManaShield ? player->getManaShield() / maxManaShield : 1) * 29);

                    g_drawPool.addFilledRect(manaShieldRect, Color::darkPink);
                }

                barsRect.moveTop(barsRect.bottom());
                g_drawPool.addFilledRect(barsRect, Color::black);

                Rect manaRect = barsRect.expanded(-1);
                const double maxMana = player->getMaxMana();
                manaRect.setWidth((maxMana ? player->getMana() / maxMana : 1) * 29);

                g_drawPool.addFilledRect(manaRect, Color::blue);
            }
        }

        backgroundRect = barsRect;
    }

    if (drawFlags & Otc::DrawHarmony && isLocalPlayer() && g_game.getFeature(Otc::GameVocationMonk)) {
        if (const auto& player = g_game.getLocalPlayer()) {
            if (player->isMonk()) {
                // Harmony
                backgroundRect.moveTop(backgroundRect.bottom());
                g_drawPool.addFilledRect(backgroundRect, Color::black);
                for (int i = 0; i < 5; i++) {
                    Rect subBarRect = backgroundRect.expanded(-1);
                    subBarRect.setX(backgroundRect.x() + 1 + i * (5 + 1));
                    subBarRect.setWidth(5);
                    Color subBarColor;
                    if (i < player->getHarmony()) {
                        subBarColor = Color(0xFF, 0x98, 0x54);
                    } else {
                        subBarColor = Color(64, 64, 64);
                    }
                    g_drawPool.addFilledRect(subBarRect, subBarColor);
                }
                // Serene
                backgroundRect.moveTop(backgroundRect.bottom());
                Rect sereneBackgroundRect(backgroundRect.center().x - (11 / 2) - 1, backgroundRect.y(), 11 + 2, backgroundRect.height() - 2 + 2);
                g_drawPool.addFilledRect(sereneBackgroundRect, Color::black);
                Color sereneColor = player->isSerene() ? Color(0xD4, 0x37, 0xFF) : Color(64, 64, 64);
                Rect sereneSubBarRect = sereneBackgroundRect.expanded(-1);
                sereneSubBarRect.setWidth(11);
                sereneSubBarRect.setHeight(backgroundRect.height() - 2);
                g_drawPool.addFilledRect(sereneSubBarRect, sereneColor);
            }
        }
    }

    g_drawPool.setDrawOrder(DrawOrder::SECOND);

    if (drawFlags & Otc::DrawNames) {
        PainterShaderProgramPtr nameProgram;
        if (!m_nameShader.empty())
            nameProgram = g_shaders.getShader(m_nameShader);

        if (nameProgram)
            g_drawPool.setShaderProgram(nameProgram);

        m_name.draw(textRect, fillColor);

        if (nameProgram)
            g_drawPool.resetShaderProgram();

        if (m_text) {
            auto extraTextSize = m_text->getTextSize();
            Rect extraTextRect = Rect(p.x - extraTextSize.width() / 2.0, p.y + 15, extraTextSize);
            m_text->drawText(extraTextRect.center(), extraTextRect);
        }
    }

    if (m_skull != Otc::SkullNone && m_skullTexture)
        g_drawPool.addTexturedPos(m_skullTexture, backgroundRect.x() + 15.5 + 12, backgroundRect.y() + 5);

    if (m_shield != Otc::ShieldNone && m_shieldTexture && m_showShieldTexture)
        g_drawPool.addTexturedPos(m_shieldTexture, backgroundRect.x() + 15.5, backgroundRect.y() + 5);

    if (m_emblem != Otc::EmblemNone && m_emblemTexture)
        g_drawPool.addTexturedPos(m_emblemTexture, backgroundRect.x() + 15.5 + 12, backgroundRect.y() + 16);

    if (m_type != Proto::CreatureTypeUnknown && m_typeTexture)
        g_drawPool.addTexturedPos(m_typeTexture, backgroundRect.x() + 15.5 + 12 + 12, backgroundRect.y() + 16);

    if (m_icon != Otc::NpcIconNone && m_iconTexture)
        g_drawPool.addTexturedPos(m_iconTexture, backgroundRect.x() + 15.5 + 12, backgroundRect.y() + 5);

    if (g_gameConfig.drawTyping() && getTyping() && m_typingIconTexture)
        g_drawPool.addTexturedPos(m_typingIconTexture, p.x + (nameSize.width() / 2.0) + 2, textRect.y() - 4);

    if (g_game.getClientVersion() >= 1281 && m_icons && !m_icons->atlasGroups.empty()) {
        int iconOffset = 0;
        for (const auto& iconTex : m_icons->atlasGroups) {
            if (!iconTex.texture) continue;
            const Rect dest(backgroundRect.x() + 15.5 + 12, backgroundRect.y() + 5 + iconOffset * 14, iconTex.clip.size());
            g_drawPool.addTexturedRect(dest, iconTex.texture, iconTex.clip);
            // draw count only when greater than 0
            if (iconTex.count > 0) {
                m_icons->numberText.setText(std::to_string(iconTex.count));
                const auto textSize = m_icons->numberText.getTextSize();
                const Rect numberRect(dest.right() + 2, dest.y() + (dest.height() - textSize.height()) / 2, textSize);
                m_icons->numberText.draw(numberRect, Color::white);
            }
            ++iconOffset;
        }
    }

    g_drawPool.resetDrawOrder();
}

void Creature::internalDraw(Point dest, const Color& color)
{
    // Example of how to send a UniformValue to shader
    /*const auto& shaderAction = [this]()-> void {
        const int id = m_outfit.isCreature() ? m_outfit.getId() : m_outfit.getAuxId();
        m_shader->bind();
        m_shader->setUniformValue(ShaderManager::OUTFIT_ID_UNIFORM, id);
    };*/

    Point originalDest = dest;

    if (!m_jumpOffset.isNull()) {
        const auto& jumpOffset = m_jumpOffset * g_drawPool.getScaleFactor();
        dest -= Point(std::round(jumpOffset.x), std::round(jumpOffset.y));
    } else if (m_bounce.height > 0 && m_bounce.speed > 0) {
        const auto minHeight = m_bounce.minHeight * g_drawPool.getScaleFactor();
        const auto height = m_bounce.height * g_drawPool.getScaleFactor();
        dest -= minHeight + (height - std::abs(height - static_cast<int>(m_bounce.timer.ticksElapsed() / (m_bounce.speed / 100.f)) % static_cast<int>(height * 2)));
    }

    const bool replaceColorShader = color != Color::white;
    if (replaceColorShader)
        g_drawPool.setShaderProgram(g_painter->getReplaceColorShader());
    else
        drawAttachedEffect(originalDest, dest, nullptr, false); // On Bottom

    if (!isHided()) {
        const int animationPhase = getCurrentAnimationPhase();

        for (const auto& paperdoll : m_paperdolls)
            paperdoll->draw(dest, animationPhase, m_outfit.hasMount(), false, true, color);

        // outfit is a real creature
        if (m_outfit.isCreature()) {
            if (m_outfit.hasMount()) {
                dest -= getMountThingType()->getDisplacement() * g_drawPool.getScaleFactor();

                if (!replaceColorShader && hasMountShader()) {
                    g_drawPool.setShaderProgram(g_shaders.getShaderById(m_mountShaderId), true/*, [this]()-> void {
                        m_mountShader->bind();
                        m_mountShader->setUniformValue(ShaderManager::MOUNT_ID_UNIFORM, m_outfit.getMount());
                    }*/);
                }
                getMountThingType()->draw(dest, 0, m_numPatternX, 0, 0, getCurrentAnimationPhase(true), color);

                dest += getDisplacement() * g_drawPool.getScaleFactor();
            }

            const auto& datType = getThingType();
            const bool useFramebuffer = !replaceColorShader && hasShader() && g_shaders.getShaderById(m_shaderId)->useFramebuffer();

            const auto& drawCreature = [&](const Point& dest) {
                // yPattern => creature addon
                for (int yPattern = 0; yPattern < getNumPatternY(); ++yPattern) {
                    // continue if we dont have this addon
                    if (yPattern > 0 && !(m_outfit.getAddons() & (1 << (yPattern - 1))))
                        continue;

                    if (!replaceColorShader && hasShader() && !useFramebuffer) {
                        g_drawPool.setShaderProgram(g_shaders.getShaderById(m_shaderId), true/*, shaderAction*/);
                    }

                    datType->draw(dest, 0, m_numPatternX, yPattern, m_numPatternZ, animationPhase, color);

                    if (m_drawOutfitColor && !replaceColorShader && getLayers() > 1) {
                        g_drawPool.setCompositionMode(CompositionMode::MULTIPLY);
                        datType->draw(dest, SpriteMaskYellow, m_numPatternX, yPattern, m_numPatternZ, animationPhase, m_outfit.getHeadColor());
                        datType->draw(dest, SpriteMaskRed, m_numPatternX, yPattern, m_numPatternZ, animationPhase, m_outfit.getBodyColor());
                        datType->draw(dest, SpriteMaskGreen, m_numPatternX, yPattern, m_numPatternZ, animationPhase, m_outfit.getLegsColor());
                        datType->draw(dest, SpriteMaskBlue, m_numPatternX, yPattern, m_numPatternZ, animationPhase, m_outfit.getFeetColor());
                        g_drawPool.resetCompositionMode();
                    }
                }
            };

            if (useFramebuffer) {
                const int size = static_cast<int>(g_gameConfig.getSpriteSize() * std::max<int>(datType->getSize().area(), 2) * g_drawPool.getScaleFactor());
                const auto& p = (Point(size) - Point(datType->getExactHeight())) / 2;
                const auto& destFB = Rect(dest - p, Size{ size });

                g_drawPool.setShaderProgram(g_shaders.getShaderById(m_shaderId), true/*, shaderAction*/);

                g_drawPool.bindFrameBuffer(destFB.size());
                drawCreature(p);
                g_drawPool.releaseFrameBuffer(destFB);
                g_drawPool.resetShaderProgram();
            } else drawCreature(dest);

            for (const auto& paperdoll : m_paperdolls)
                paperdoll->draw(dest, animationPhase, m_outfit.hasMount(), true, true, color);

            // outfit is a creature imitating an item or the invisible effect
        } else {
            int animationPhases = getThingType()->getAnimationPhases();
            int animateTicks = g_gameConfig.getItemTicksPerFrame();

            // when creature is an effect we cant render the first and last animation phase,
            // instead we should loop in the phases between
            if (m_outfit.isEffect()) {
                animationPhases = std::max<int>(1, animationPhases - 2);
                animateTicks = g_gameConfig.getInvisibleTicksPerFrame();
            }

            int animationPhase = 0;
            if (auto* animator = getThingType()->getIdleAnimator(); animator && m_outfit.isItem()) {
                animationPhase = animator->getPhase();
            } else if (animationPhases > 1) {
                animationPhase = (g_clock.millis() % (static_cast<long long>(animateTicks) * animationPhases)) / animateTicks;
            }

            if (m_outfit.isEffect())
                animationPhase = std::min<int>(animationPhase + 1, animationPhases);

            if (!replaceColorShader && hasShader())
                g_drawPool.setShaderProgram(g_shaders.getShaderById(m_shaderId), true/*, shaderAction*/);
            getThingType()->draw(dest - (getDisplacement() * g_drawPool.getScaleFactor()), 0, 0, 0, 0, animationPhase, color);
        }
    }

    if (replaceColorShader)
        g_drawPool.resetShaderProgram();
    else {
        drawAttachedEffect(originalDest, dest, nullptr, true); // On Top
        drawAttachedParticlesEffect(originalDest);
    }
}

void Creature::turn(const Otc::Direction direction)
{
    setDirection(direction);
}

void Creature::setSubTilePosition(uint8_t subX, uint8_t subY)
{
    // During prediction, ignore server sub-tile updates entirely.
    // The client prediction is ahead by network latency; applying server values
    // would cause the character to snap backward. Server values are stale by design.
    // Tile crossings are handled separately via onAppear().
    if (m_isPredicting) {
        return;
    }

    // After prediction stops, server sub-tile updates may still be stale
    // (the server hasn't processed our stop yet). Ignore them briefly to
    // prevent snap-back.
    if (m_predictionCooldown) {
        if (m_predictionCooldownTimer.ticksElapsed() < 150) {
            return;
        }
        m_predictionCooldown = false;
    }

    const uint8_t oldSubX = m_subTileX;
    const uint8_t oldSubY = m_subTileY;

    m_subTileX = subX;
    m_subTileY = subY;

    // If sub-tile position changed, infer movement direction and update walk animation
    if (oldSubX != subX || oldSubY != subY) {
        // Server is moving us — clear collision suppression
        m_collisionSuppressed = false;
        m_collisionDirection = Otc::InvalidDirection;

        const int dx = static_cast<int>(subX) - static_cast<int>(oldSubX);
        const int dy = static_cast<int>(subY) - static_cast<int>(oldSubY);

        Otc::Direction dir = Otc::InvalidDirection;
        if (dx > 0 && dy < 0) dir = Otc::NorthEast;
        else if (dx > 0 && dy > 0) dir = Otc::SouthEast;
        else if (dx < 0 && dy < 0) dir = Otc::NorthWest;
        else if (dx < 0 && dy > 0) dir = Otc::SouthWest;
        else if (dx > 0) dir = Otc::East;
        else if (dx < 0) dir = Otc::West;
        else if (dy < 0) dir = Otc::North;
        else if (dy > 0) dir = Otc::South;

        if (dir != Otc::InvalidDirection) {
            setDirection(dir);
            m_lastStepDirection = dir;
            if (!m_subTileMoving) {
                m_footTimer.restart();
            }
            m_subTileMoving = true;
            m_subTileMoveTimer.restart();
        }

        updateWalkAnimation();

        if (isCameraFollowing()) {
            g_map.notificateCameraMove(getSubTileOffset());
        }
    }
}

std::pair<float, float> Creature::getPredictedSubTileF() const
{
    float subX = static_cast<float>(m_subTileX);
    float subY = static_cast<float>(m_subTileY);

    // Sub-tile transition: interpolate from start position to center (128,128)
    if (m_subTileTransitioning && m_subTileTransitionDuration > 0) {
        const float progress = std::min(1.0f, static_cast<float>(m_subTileTransitionTimer.ticksElapsed()) / static_cast<float>(m_subTileTransitionDuration));
        subX = m_subTileTransitionStartX + (128.0f - m_subTileTransitionStartX) * progress;
        subY = m_subTileTransitionStartY + (128.0f - m_subTileTransitionStartY) * progress;
        return { subX, subY };
    }

    if (m_isPredicting && m_predictionDirection != Otc::InvalidDirection && m_predictionStepDuration > 0) {
        const float elapsed = m_predictionTimer.ticksElapsed();
        const float subTilesPerMs = 255.0f / static_cast<float>(m_predictionStepDuration);
        const float delta = subTilesPerMs * elapsed;
        const bool diag = Position::isDiagonal(m_predictionDirection);
        const float axisDelta = diag ? delta * 0.7071f : delta;

        float dx = 0.0f, dy = 0.0f;
        switch (m_predictionDirection) {
            case Otc::North: dy = -axisDelta; break;
            case Otc::South: dy = axisDelta; break;
            case Otc::West: dx = -axisDelta; break;
            case Otc::East: dx = axisDelta; break;
            case Otc::NorthEast: dx = axisDelta; dy = -axisDelta; break;
            case Otc::NorthWest: dx = -axisDelta; dy = -axisDelta; break;
            case Otc::SouthEast: dx = axisDelta; dy = axisDelta; break;
            case Otc::SouthWest: dx = -axisDelta; dy = axisDelta; break;
            default: break;
        }

        subX += dx;
        subY += dy;

        // Only clamp at tile boundaries when the next tile is not walkable.
        // This prevents the character from visually passing through walls.
        // For walkable tiles, prediction is allowed past boundaries for smooth
        // movement; onAppear() handles the tile crossing shift.
        // If the server rejects a move the client thought was valid, the server
        // sends its corrected sub-tile position alongside cancelWalk.
        if (!m_predictionNextTileWalkable) {
            subX = std::clamp(subX, 0.0f, 255.0f);
            subY = std::clamp(subY, 0.0f, 255.0f);
        }
    }

    return { subX, subY };
}

Point Creature::getSubTileOffset() const
{
    auto [subX, subY] = getPredictedSubTileF();
    const int spriteSize = g_gameConfig.getSpriteSize();
    // Convert sub-tile (0-255) to pixel offset relative to tile center
    // 128 = center (0 offset), 0 = -spriteSize/2, 255 = +spriteSize/2
    const int offsetX = static_cast<int>((subX / 255.0f - 0.5f) * spriteSize);
    const int offsetY = static_cast<int>((subY / 255.0f - 0.5f) * spriteSize);
    return { offsetX, offsetY };
}

void Creature::startMovementPrediction(Otc::Direction dir)
{
    // After a collision (cancelWalk), suppress visual prediction in the same
    // direction to prevent flickering. The walk packet is still sent to the
    // server, but no visual prediction occurs until the server confirms a
    // successful move or the player changes direction.
    if (m_collisionSuppressed) {
        if (dir == m_collisionDirection) {
            return;
        }
        // Different direction — clear suppression and proceed normally
        m_collisionSuppressed = false;
        m_collisionDirection = Otc::InvalidDirection;
    }

    // Already predicting in the same direction — don't restart timer
    if (m_isPredicting && m_predictionDirection == dir)
        return;

    // If already predicting in a different direction, capture current predicted
    // position as the new base before changing direction
    if (m_isPredicting) {
        auto [subX, subY] = getPredictedSubTileF();
        m_subTileX = static_cast<uint8_t>(std::clamp(subX, 0.0f, 255.0f));
        m_subTileY = static_cast<uint8_t>(std::clamp(subY, 0.0f, 255.0f));
    }

    // Set direction so the character faces the intended direction even if
    // the tile is blocked (visual feedback: face the wall).
    setDirection(dir);
    m_lastStepDirection = dir;

    // Client-side walkability check — if the next tile is known-blocked,
    // skip visual prediction entirely to avoid the predict-forward → snap-back
    // artifact. The walk packet is still sent by the caller (forceWalk),
    // so the server remains the authority.
    if (!checkNextTileWalkable(dir)) {
        if (m_isPredicting) {
            m_isPredicting = false;
            m_predictionDirection = Otc::InvalidDirection;
            m_subTileMoving = false;
            m_walkAnimationPhase = 0;
        }
        m_collisionSuppressed = true;
        m_collisionDirection = dir;
        if (isCameraFollowing()) {
            g_map.notificateCameraMove(getSubTileOffset());
        }
        return;
    }

    m_predictionDirection = dir;
    m_predictionStepDuration = getStepDuration(false, dir);
    m_predictionNextTileWalkable = true;

    // Start walk animation immediately for instant visual feedback
    // Sub-tile movement uses m_subTileMoving for animation control.
    if (!m_subTileMoving) {
        m_footTimer.restart();
    }
    m_subTileMoving = true;
    m_subTileMoveTimer.restart();
    m_predictionCooldown = false;
    updateWalkAnimation();

    if (!m_isPredicting) {
        m_isPredicting = true;
    }
    m_predictionTimer.restart();
}

void Creature::stopMovementPrediction()
{
    // Capture current predicted visual position so the character doesn't
    // snap back to the old m_subTileX/Y base when prediction stops
    if (m_isPredicting) {
        auto [subX, subY] = getPredictedSubTileF();
        m_subTileX = static_cast<uint8_t>(std::clamp(subX, 0.0f, 255.0f));
        m_subTileY = static_cast<uint8_t>(std::clamp(subY, 0.0f, 255.0f));
    }

    m_isPredicting = false;
    m_predictionDirection = Otc::InvalidDirection;
    m_predictionNextTileWalkable = true;
    m_subTileMoving = false;
    m_walkAnimationPhase = 0;

    // Clear collision suppression when the player deliberately stops
    m_collisionSuppressed = false;
    m_collisionDirection = Otc::InvalidDirection;

    // Start cooldown: ignore stale server sub-tile updates that arrive
    // before the server has processed our stop request
    m_predictionCooldown = true;
    m_predictionCooldownTimer.restart();

    // Ensure the camera recalculates its srcRect with the captured sub-tile
    // position. Without this, isMoving() returns false and updateRect skips
    // requestUpdateMapPosInfo(), leaving the srcRect stale.
    if (isCameraFollowing()) {
        g_map.notificateCameraMove(getSubTileOffset());
    }
}

void Creature::rejectMovementPrediction()
{
    // Unlike stopMovementPrediction, this does NOT capture the current
    // predicted position. Used when the server rejects a move (cancelWalk).
    // The character snaps back to its pre-prediction base position (m_subTileX/Y).
    const auto rejectedDir = m_predictionDirection;

    m_isPredicting = false;
    m_predictionDirection = Otc::InvalidDirection;
    m_predictionNextTileWalkable = true;
    m_subTileMoving = false;
    m_walkAnimationPhase = 0;

    // Suppress future visual prediction in the rejected direction to prevent
    // the predict-forward → snap-back flickering loop on retries.
    m_collisionSuppressed = true;
    m_collisionDirection = rejectedDir;

    // Notify camera of the corrected position (back to base sub-tile)
    if (isCameraFollowing()) {
        g_map.notificateCameraMove(getSubTileOffset());
    }
}

void Creature::resetContinuousMovementState()
{
    // Reset all prediction state
    m_isPredicting = false;
    m_predictionDirection = Otc::InvalidDirection;
    m_predictionNextTileWalkable = true;

    // Reset sub-tile movement state
    m_subTileMoving = false;
    m_walkAnimationPhase = 0;

    // Reset sub-tile transition
    m_subTileTransitioning = false;

    // Reset sub-tile position to tile center
    m_subTileX = 128;
    m_subTileY = 128;

    // Clear cooldown and collision suppression
    m_predictionCooldown = false;
    m_collisionSuppressed = false;
    m_collisionDirection = Otc::InvalidDirection;
}

void Creature::startSubTileTransition(Otc::Direction dir)
{
    const int dx = Position::isDiagonal(dir) ? (dir == Otc::NorthEast || dir == Otc::SouthEast ? 1 : -1) : (dir == Otc::East ? 1 : (dir == Otc::West ? -1 : 0));
    const int dy = Position::isDiagonal(dir) ? (dir == Otc::SouthEast || dir == Otc::SouthWest ? 1 : -1) : (dir == Otc::South ? 1 : (dir == Otc::North ? -1 : 0));

    // Entry edge: coming from the opposite side
    m_subTileTransitionStartX = dx > 0 ? 0.0f : (dx < 0 ? 255.0f : 128.0f);
    m_subTileTransitionStartY = dy > 0 ? 0.0f : (dy < 0 ? 255.0f : 128.0f);

    m_subTileX = static_cast<uint8_t>(m_subTileTransitionStartX);
    m_subTileY = static_cast<uint8_t>(m_subTileTransitionStartY);

    m_subTileTransitionDuration = getStepDuration(false, dir);
    m_subTileTransitionTimer.restart();
    m_subTileTransitioning = true;

    if (!m_subTileMoving) {
        m_footTimer.restart();
    }
    m_subTileMoving = true;
    m_subTileMoveTimer.restart();
}

bool Creature::checkNextTileWalkable(Otc::Direction dir) const
{
    const Position nextPos = m_position.translatedToDirection(dir);
    const auto nextTile = g_map.getTile(nextPos);
    if (!nextTile || !nextTile->isWalkable(true))
        return false;

    // For diagonal directions, check that at least one intermediate
    // cardinal tile is passable. This prevents clipping through wall corners.
    if (Position::isDiagonal(dir)) {
        Otc::Direction cardinalX = Otc::InvalidDirection;
        Otc::Direction cardinalY = Otc::InvalidDirection;

        switch (dir) {
            case Otc::NorthEast: cardinalX = Otc::East;  cardinalY = Otc::North; break;
            case Otc::NorthWest: cardinalX = Otc::West;  cardinalY = Otc::North; break;
            case Otc::SouthEast: cardinalX = Otc::East;  cardinalY = Otc::South; break;
            case Otc::SouthWest: cardinalX = Otc::West;  cardinalY = Otc::South; break;
            default: break;
        }

        bool xBlocked = true;
        bool yBlocked = true;

        if (cardinalX != Otc::InvalidDirection) {
            const auto tileX = g_map.getTile(m_position.translatedToDirection(cardinalX));
            if (tileX && tileX->isWalkable(true))
                xBlocked = false;
        }

        if (cardinalY != Otc::InvalidDirection) {
            const auto tileY = g_map.getTile(m_position.translatedToDirection(cardinalY));
            if (tileY && tileY->isWalkable(true))
                yBlocked = false;
        }

        // Both intermediate tiles blocked — can't squeeze through the corner
        if (xBlocked && yBlocked)
            return false;
    }

    return true;
}

void Creature::jump(const int height, const int duration)
{
    if (!m_jumpOffset.isNull())
        return;

    m_jumpTimer.restart();
    m_jumpHeight = height;
    m_jumpDuration = duration;

    updateJump();
}

void Creature::updateJump()
{
    if (m_jumpTimer.ticksElapsed() >= m_jumpDuration) {
        m_jumpOffset = PointF();
        return;
    }

    const int t = m_jumpTimer.ticksElapsed();
    const double a = -4 * m_jumpHeight / (m_jumpDuration * m_jumpDuration);
    const double b = +4 * m_jumpHeight / m_jumpDuration;
    const double height = a * t * t + b * t;

    const int roundHeight = std::round(height);
    const int halfJumpDuration = m_jumpDuration / 2;

    m_jumpOffset = PointF(height, height);

    if (isCameraFollowing()) {
        g_map.notificateCameraMove(getSubTileOffset());
    }

    int nextT = 0;
    int diff = 0;
    int i = 1;
    if (m_jumpTimer.ticksElapsed() < halfJumpDuration)
        diff = 1;
    else if (m_jumpTimer.ticksElapsed() > halfJumpDuration)
        diff = -1;

    do {
        nextT = std::round((-b + std::sqrt(std::max<double>(b * b + 4 * a * (roundHeight + diff * i), 0.0)) * diff) / (2 * a));
        ++i;

        if (nextT < halfJumpDuration)
            diff = 1;
        else if (nextT > halfJumpDuration)
            diff = -1;
    } while (nextT - m_jumpTimer.ticksElapsed() == 0 && i < 3);

    // schedules next update
    const auto self = static_self_cast<Creature>();
    g_dispatcher.scheduleEvent([self] {
        self->updateJump();
    }, nextT - m_jumpTimer.ticksElapsed());
}

void Creature::onPositionChange(const Position& newPos, const Position& oldPos)
{
    // Tile change means server accepted movement — clear collision suppression
    m_collisionSuppressed = false;
    m_collisionDirection = Otc::InvalidDirection;

    callLuaFieldUnchecked("onPositionChange", newPos, oldPos);
}

void Creature::onAppear()
{
    // cancel any disappear event
    if (m_disappearEvent) {
        m_disappearEvent->cancel();
        m_disappearEvent = nullptr;
    }

    // creature appeared the first time or wasn't seen for a long time
    if (m_removed) {
        resetContinuousMovementState();
        m_removed = false;
        if (isCameraFollowing()) {
            g_map.notificateCameraMove(getSubTileOffset());
        }
        callLuaField("onAppear");
    } // creature moved to adjacent tile
    else if (m_oldPosition != m_position && m_oldPosition.isInRange(m_position, 1, 1) && m_allowAppearWalk) {
        m_allowAppearWalk = false;
        const auto dir = m_oldPosition.getDirectionFromPosition(m_position);
        setDirection(dir);
        m_lastStepDirection = dir;
        const int dx = m_position.x - m_oldPosition.x;
        const int dy = m_position.y - m_oldPosition.y;
        if (m_isPredicting) {
            // Preserve visual continuity: compute current unclamped predicted
            // position and shift by one tile to get the new base on the new tile.
            auto [predSubX, predSubY] = getPredictedSubTileF();
            predSubX -= dx * 255.0f;
            predSubY -= dy * 255.0f;
            m_subTileX = static_cast<uint8_t>(std::clamp(predSubX, 0.0f, 255.0f));
            m_subTileY = static_cast<uint8_t>(std::clamp(predSubY, 0.0f, 255.0f));
            m_predictionTimer.restart();

            // Recheck walkability and recalculate step duration for the new tile
            m_predictionNextTileWalkable = checkNextTileWalkable(m_predictionDirection);
            m_predictionStepDuration = getStepDuration(false, m_predictionDirection);
        } else {
            // Non-predicting creature (monster/NPC/other player): start sub-tile transition
            // from edge to center for smooth visual movement
            startSubTileTransition(dir);
        }
        if (isCameraFollowing()) {
            g_map.notificateCameraMove(getSubTileOffset());
        }
        callLuaField("onWalk", m_oldPosition, m_position);
    } // teleport
    else if (m_oldPosition != m_position) {
        resetContinuousMovementState();
        if (isCameraFollowing()) {
            g_map.notificateCameraMove(getSubTileOffset());
        }
        callLuaField("onDisappear");
        callLuaField("onAppear");
    } // else turn
}

void Creature::onDisappear()
{
    if (m_disappearEvent)
        m_disappearEvent->cancel();

    m_oldPosition = m_position;

    // a pair onDisappear and onAppear events are fired even when creatures walks or turns,
    // so we must filter
    const auto self = static_self_cast<Creature>();
    m_disappearEvent = g_dispatcher.addEvent([self] {
        self->m_removed = true;
        self->resetContinuousMovementState();

        self->callLuaField("onDisappear");

        // invalidate this creature position
        if (!self->isLocalPlayer())
            self->setPosition(Position());

        self->m_oldPosition = {};
        self->m_disappearEvent = nullptr;

        if (g_game.getAttackingCreature() == self)
            g_game.cancelAttack();
        else if (g_game.getFollowingCreature() == self)
            g_game.cancelFollow();
    });

    Thing::onDisappear();
}

void Creature::onDeath()
{
    callLuaField("onDeath");
}

void Creature::updateWalkAnimation()
{
    if (!m_outfit.isCreature())
        return;

    int footAnimPhases = m_outfit.hasMount() ? getMountThingType()->getAnimationPhases() : getAnimationPhases();
    if (!g_game.getFeature(Otc::GameEnhancedAnimations) && footAnimPhases > 2) {
        --footAnimPhases;
    }

    // looktype has no animations
    if (footAnimPhases == 0)
        return;

    int minFootDelay = 20;
    const int maxFootDelay = footAnimPhases > 2 ? 80 : 205;
    int footAnimDelay = footAnimPhases;

    if (g_game.getFeature(Otc::GameEnhancedAnimations) && footAnimPhases > 2) {
        minFootDelay += 10;
        if (footAnimDelay > 1)
            footAnimDelay /= 1.5;
    }

    const auto walkSpeed = m_walkingAnimationSpeed > 0 ? m_walkingAnimationSpeed : m_stepCache.getDuration(m_lastStepDirection);
    const int footDelay = std::clamp<int>(walkSpeed / footAnimDelay, minFootDelay, maxFootDelay);

    if (m_footTimer.ticksElapsed() >= footDelay) {
        if (m_walkAnimationPhase == footAnimPhases) m_walkAnimationPhase = 1;
        else ++m_walkAnimationPhase;

        m_footTimer.restart();
    }
}

void Creature::setHealthPercent(const uint8_t healthPercent)
{
    static constexpr Color
        COLOR1(0x00, 0xBC, 0x00),
        COLOR2(0x50, 0xA1, 0x50),
        COLOR3(0xA1, 0xA1, 0x00),
        COLOR4(0xBF, 0x0A, 0x0A),
        COLOR5(0x91, 0x0F, 0x0F),
        COLOR6(0x85, 0x0C, 0x0C);

    if (m_healthPercent == healthPercent) return;

    if (healthPercent > 92)
        m_informationColor = COLOR1;
    else if (healthPercent > 60)
        m_informationColor = COLOR2;
    else if (healthPercent > 30)
        m_informationColor = COLOR3;
    else if (healthPercent > 8)
        m_informationColor = COLOR4;
    else if (healthPercent > 3)
        m_informationColor = COLOR5;
    else
        m_informationColor = COLOR6;

    const uint8_t oldHealthPercent = m_healthPercent;
    m_healthPercent = healthPercent;

    callLuaField("onHealthPercentChange", healthPercent, oldHealthPercent);

    if (isDead())
        onDeath();
}

void Creature::setDirection(const Otc::Direction direction)
{
    if (direction == Otc::InvalidDirection)
        return;

    m_direction = direction;

    // xPattern => creature direction
    if (direction == Otc::NorthEast || direction == Otc::SouthEast)
        m_numPatternX = Otc::East;
    else if (direction == Otc::NorthWest || direction == Otc::SouthWest)
        m_numPatternX = Otc::West;
    else
        m_numPatternX = direction;

    setAttachedEffectDirection(static_cast<Otc::Direction>(m_numPatternX));
    setPaperdollsDirection(static_cast<Otc::Direction>(m_numPatternX));
}

void Creature::setOutfit(const Outfit& outfit, bool fireEvent)
{
    if (m_outfit == outfit)
        return;

    Outfit newOutfit = outfit;
    if (newOutfit.isInvalid()) {
        newOutfit.setCategory(newOutfit.getAuxId() > 0 ? ThingCategoryItem : ThingCategoryCreature);
    }

    const Outfit oldOutfit = m_outfit;

    m_outfit = newOutfit;
    m_numPatternZ = 0;
    m_exactSize = 0;
    if (m_walkingAnimationSpeed == 0) {
        m_walkAnimationPhase = 0; // might happen when player is walking and outfit is changed.
    }

    if (m_outfit.isInvalid())
        m_outfit.setCategory(m_outfit.getAuxId() > 0 ? ThingCategoryItem : ThingCategoryCreature);

    const auto thingType = getThingType();
    if (!thingType) {
        g_logger.error("Creature::setOutfit - Invalid thing type for creature {}.", getId());
        m_outfit = oldOutfit;
        return;
    }

    m_clientId = thingType->getId();

    if (m_outfit.hasMount()) {
        m_numPatternZ = std::min<int>(1, getNumPatternZ() - 1);
    }

    if ((g_game.getFeature(Otc::GameWingsAurasEffectsShader))) {
        m_outfit.setWing(0);
        m_outfit.setAura(0);
        m_outfit.setEffect(0);
        m_outfit.setShader("Outfit - Default");
    }

    if (const auto& tile = getTile())
        tile->checkForDetachableThing();

    if (fireEvent)
        callLuaField("onOutfitChange", m_outfit, oldOutfit);
}

void Creature::setSpeed(uint16_t speed)
{
    if (speed == m_speed)
        return;

    const uint16_t oldSpeed = m_speed;
    m_speed = speed;

    // Cache for stepSpeed Law
    if (hasSpeedFormula()) {
        speed *= 2;

        if (speed > -speedB) {
            m_calculatedStepSpeed = std::max<int>(1, floor((speedA * log((speed / 2.) + speedB) + speedC) + .5));
        } else m_calculatedStepSpeed = 1;
    }

    // Recalculate prediction step duration if speed changes during movement
    if (isLocalPlayer() && m_isPredicting) {
        m_predictionStepDuration = getStepDuration(false, m_predictionDirection);
    }

    callLuaField("onSpeedChange", m_speed, oldSpeed);
}

void Creature::setBaseSpeed(const uint16_t baseSpeed)
{
    if (m_baseSpeed == baseSpeed)
        return;

    const uint16_t oldBaseSpeed = m_baseSpeed;
    m_baseSpeed = baseSpeed;

    callLuaField("onBaseSpeedChange", baseSpeed, oldBaseSpeed);
}

void Creature::setType(const uint8_t v) { if (m_type != v) callLuaField("onTypeChange", m_type = v); }
void Creature::setIcon(const uint8_t v) { if (m_icon != v) callLuaField("onIconChange", m_icon = v); }
void Creature::setIcons(const std::vector<std::tuple<uint8_t, uint8_t, uint16_t>>& icons)
{
    if (!m_icons) {
        m_icons = std::make_unique<IconRenderData>();
        m_icons->numberText.setFont(g_gameConfig.getStaticTextFont());
        m_icons->numberText.setAlign(Fw::AlignCenter);
    }

    m_icons->atlasGroups.clear();
    m_icons->iconEntries = icons;

    for (const auto& [icon, category, count] : icons) {
        callLuaField("onIconsChange", icon, category, count);
    }
}
void Creature::setSkull(const uint8_t v) { if (m_skull != v) callLuaField("onSkullChange", m_skull = v); }
void Creature::setShield(const uint8_t v) { if (m_shield != v) callLuaField("onShieldChange", m_shield = v); }
void Creature::setEmblem(const uint8_t v) { if (m_emblem != v) callLuaField("onEmblemChange", m_emblem = v); }

void Creature::setTypeTexture(const std::string& filename) { m_typeTexture = g_textures.getTexture(filename); }
void Creature::setIconTexture(const std::string& filename) { m_iconTexture = g_textures.getTexture(filename); }
void Creature::setIconsTexture(const std::string& filename, const Rect& clip, const uint16_t count)
{
    if (!m_icons) {
        m_icons = std::make_unique<IconRenderData>();
        m_icons->numberText.setFont(g_gameConfig.getStaticTextFont());
        m_icons->numberText.setAlign(Fw::AlignCenter);
    }

    m_icons->atlasGroups.emplace_back(IconRenderData::AtlasIconGroup{ g_textures.getTexture(filename), clip, count });
}
void Creature::setSkullTexture(const std::string& filename) { m_skullTexture = g_textures.getTexture(filename); }
void Creature::setEmblemTexture(const std::string& filename) { m_emblemTexture = g_textures.getTexture(filename); }

void Creature::setShieldTexture(const std::string& filename, const bool blink)
{
    m_shieldTexture = g_textures.getTexture(filename);
    m_showShieldTexture = true;

    if (blink && !m_shieldBlink) {
        auto self = static_self_cast<Creature>();
        g_dispatcher.scheduleEvent([self] {
            self->updateShield();
        }, g_gameConfig.getShieldBlinkTicks());
    }

    m_shieldBlink = blink;
}

void Creature::addTimedSquare(const uint8_t color)
{
    m_showTimedSquare = true;
    m_timedSquareColor = Color::from8bit(color != 0 ? color : 1);

    // schedule removal
    const auto self = static_self_cast<Creature>();
    g_dispatcher.scheduleEvent([self] {
        self->removeTimedSquare();
    }, g_gameConfig.getVolatileSquareDuration());
}

void Creature::updateShield()
{
    m_showShieldTexture = !m_showShieldTexture;

    if (m_shield != Otc::ShieldNone && m_shieldBlink) {
        auto self = static_self_cast<Creature>();
        g_dispatcher.scheduleEvent([self] {
            self->updateShield();
        }, g_gameConfig.getShieldBlinkTicks());
    } else if (!m_shieldBlink)
        m_showShieldTexture = true;
}

int Creature::getDrawElevation() {
    if (const auto& tile = g_map.getTile(getPosition()))
        return tile->getDrawElevation();

    return 0;
}

bool Creature::hasSpeedFormula() { return g_game.getFeature(Otc::GameNewSpeedLaw) && speedA != 0 && speedB != 0 && speedC != 0; }

uint16_t Creature::getStepDuration(const bool ignoreDiagonal, const Otc::Direction dir)
{
    if (m_speed < 1)
        return 0;

    const auto& tilePos = dir == Otc::InvalidDirection ?
        getPosition() : getPosition().translatedToDirection(dir);

    const auto& tile = g_map.getTile(tilePos.isValid() ? tilePos : getPosition());

    const int serverBeat = g_game.getServerBeat();

    int groundSpeed = 0;
    if (tile) groundSpeed = tile->getGroundSpeed();
    if (groundSpeed == 0)
        groundSpeed = 150;

    if (groundSpeed != m_stepCache.groundSpeed || m_speed != m_stepCache.speed) {
        m_stepCache.speed = m_speed;
        m_stepCache.groundSpeed = groundSpeed;

        uint32_t stepDuration = 1000 * groundSpeed;
        if (hasSpeedFormula()) {
            stepDuration /= m_calculatedStepSpeed;
        } else stepDuration /= m_speed;

        if (g_gameConfig.isForcingNewWalkingFormula() || g_game.getClientVersion() >= 860) {
            stepDuration = ((stepDuration + serverBeat - 1) / serverBeat) * serverBeat;
        }

        m_stepCache.duration = stepDuration;

        m_stepCache.walkDuration = std::min<int>(stepDuration / g_gameConfig.getSpriteSize(), DrawPool::FPS60);

        m_stepCache.diagonalDuration = stepDuration;
    }

    auto duration = ignoreDiagonal ? m_stepCache.duration : m_stepCache.getDuration(m_lastStepDirection);

    return duration;
}

Point Creature::getDisplacement() const
{
    if (m_outfit.isEffect())
        return { 8 };

    if (m_outfit.isItem())
        return {};

    return Thing::getDisplacement();
}

int Creature::getDisplacementX() const
{
    if (m_outfit.isEffect())
        return 8;

    if (m_outfit.isItem())
        return 0;

    if (m_outfit.hasMount())
        return getMountThingType()->getDisplacementX();

    return Thing::getDisplacementX();
}

int Creature::getDisplacementY() const
{
    if (m_outfit.isEffect())
        return 8;

    if (m_outfit.isItem())
        return 0;

    if (m_outfit.hasMount())
        return getMountThingType()->getDisplacementY();

    return Thing::getDisplacementY();
}

const Light& Creature::getLight() const
{
    const auto& light = Thing::getLight();
    return m_light.color > 0 && m_light.intensity >= light.intensity ? m_light : light;
}

ThingType* Creature::getThingType() const {
    return g_things.getRawThingType(m_outfit.isCreature() ? m_outfit.getId() : m_outfit.getAuxId(), m_outfit.getCategory());
}

ThingType* Creature::getMountThingType() const {
    return m_outfit.hasMount() ? g_things.getRawThingType(m_outfit.getMount(), ThingCategoryCreature) : nullptr;
}

uint16_t Creature::getCurrentAnimationPhase(const bool mount)
{
    if (!canAnimate()) return 0;

    const auto thingType = mount ? getMountThingType() : getThingType();

    if (const auto idleAnimator = thingType->getIdleAnimator()) {
        if (m_walkAnimationPhase == 0) return idleAnimator->getPhase();
        return m_walkAnimationPhase + idleAnimator->getAnimationPhases() - 1;
    }

    if (thingType->isAnimateAlways()) {
        const int ticksPerFrame = std::round(1000 / thingType->getAnimationPhases());
        return (g_clock.millis() % (static_cast<long long>(ticksPerFrame) * thingType->getAnimationPhases())) / ticksPerFrame;
    }

    return isDisabledWalkAnimation() ? 0 : m_walkAnimationPhase;
}

int Creature::getExactSize(int layer, int /*xPattern*/, int yPattern, int zPattern, int /*animationPhase*/)
{
    if (m_exactSize > 0)
        return m_exactSize;

    uint8_t exactSize = 0;
    if (m_outfit.isCreature()) {
        const int layers = getLayers();

        zPattern = m_outfit.hasMount() ? 1 : 0;

        if (yPattern > 0) {
            for (int pattern = 0; pattern < yPattern; ++pattern) {
                if (pattern > 0 && !(m_outfit.getAddons() & (1 << (yPattern - 1))))
                    continue;

                for (layer = 0; layer < layers; ++layer)
                    exactSize = std::max<int>(exactSize, Thing::getExactSize(layer, 0, yPattern, zPattern, 0));
            }
        } else {
            for (layer = 0; layer < layers; ++layer)
                exactSize = std::max<int>(exactSize, Thing::getExactSize(layer, 0, yPattern, zPattern, 0));
        }
    } else {
        exactSize = getThingType()->getExactSize();
    }

    return m_exactSize = std::max<uint8_t>(exactSize, g_gameConfig.getSpriteSize());
}

void Creature::setMountShader(const std::string_view name) {
    m_mountShaderId = 0;
    if (name.empty())
        return;

    if (const auto& shader = g_shaders.getShader(name))
        m_mountShaderId = shader->getId();
}

void Creature::setTypingIconTexture(const std::string& filename)
{
    m_typingIconTexture = g_textures.getTexture(filename);
}

void Creature::setTyping(const bool typing)
{
    m_typing = typing;
}

void Creature::sendTyping() {
    g_game.sendTyping(m_typing);
}

void Creature::onStartAttachEffect(const AttachedEffectPtr& effect) {
    if (effect->isDisabledWalkAnimation()) {
        setDisableWalkAnimation(true);
    }

    if (effect->getThingType() && (effect->getThingType()->isCreature() || effect->getThingType()->isMissile()))
        effect->m_direction = getDirection();
}

void Creature::onDispatcherAttachEffect(const AttachedEffectPtr& effect) {
    if (effect->isTransform() && effect->getThingType()) {
        const auto& outfit = getOutfit();
        if (outfit.isTemp())
            return;

        effect->m_outfitOwner = outfit;

        Outfit newOutfit = outfit;
        newOutfit.setTemp(true);
        newOutfit.setCategory(effect->getThingType()->getCategory());
        if (newOutfit.isCreature())
            newOutfit.setId(effect->getThingType()->getId());
        else
            newOutfit.setAuxId(effect->getThingType()->getId());

        setOutfit(newOutfit);
    }
}

void Creature::onStartDetachEffect(const AttachedEffectPtr& effect) {
    if (effect->isDisabledWalkAnimation())
        setDisableWalkAnimation(false);

    if (effect->isTransform() && !effect->m_outfitOwner.isInvalid()) {
        setOutfit(effect->m_outfitOwner);
    }
}

void Creature::setStaticWalking(const uint16_t v) {
    if (!canDraw())
        return;

    m_walkingAnimationSpeed = v;

    if (v == 0) {
        m_walkAnimationPhase = 0;
        m_subTileMoving = false;
        return;
    }

    // Static walking uses sub-tile moving to drive walk animation each frame
    if (!m_subTileMoving) {
        m_footTimer.restart();
    }
    m_subTileMoving = true;
    m_subTileMoveTimer.restart();
}

void Creature::setWidgetInformation(const UIWidgetPtr& info) {
    if (m_widgetInformation == info)
        return;

    if (m_widgetInformation && !m_widgetInformation->isDestroyed()) {
        g_map.removeAttachedWidgetFromObject(m_widgetInformation);
    }

    m_widgetInformation = info;

    if (!info)
        return;

    info->setDraggable(false);
    g_map.addAttachedWidgetToObject(info, std::static_pointer_cast<AttachableObject>(shared_from_this()));
}

void Creature::setName(const std::string_view name) {
    if (name == m_name.getText())
        return;

    const auto& oldName = m_name.getText();
    m_name.setText(name);
    callLuaField("onChangeName", name, oldName);
}

void Creature::setCovered(bool covered) {
    if (m_isCovered == covered)
        return;

    const auto oldCovered = m_isCovered;
    m_isCovered = covered;

    g_dispatcher.addEvent([self = static_self_cast<Creature>(), covered, oldCovered] {
        self->callLuaField("onCovered", covered, oldCovered);
    });
}

void Creature::setText(const std::string& text, const Color& color)
{
    if (!m_text) {
        m_text = std::make_shared<StaticText>();
    }
    m_text->setText(text);
    m_text->setColor(color);
}

std::string Creature::getText()
{
    if (!m_text) {
        return "";
    }
    return m_text->getText();
}

bool Creature::canShoot(int distance)
{
    return getTile() ? getTile()->canShoot(distance) : false;
}

bool Creature::hasPaperdoll(uint16_t id) {
    for (const auto& pd : m_paperdolls) {
        if (pd->m_id == id)
            return true;
    }

    return false;
}

void Creature::attachPaperdoll(const PaperdollPtr& obj) {
    if (!obj) return;

    obj->m_direction = getDirection();

    uint_fast8_t i = 0;
    for (const auto& pd : m_paperdolls) {
        if (obj->m_priority < pd->m_priority)
            break;
        ++i;
    }

    m_paperdolls.insert(m_paperdolls.begin() + i, obj);

    g_dispatcher.addEvent([paperdoll = obj, self = static_self_cast<Thing>()] {
        paperdoll->callLuaField("onAttach", self->asLuaObject());
    });
}

bool Creature::detachPaperdollById(uint16_t id) {
    const auto it = std::find_if(m_paperdolls.begin(), m_paperdolls.end(),
                                 [id](const PaperdollPtr& obj) { return obj->getId() == id; });

    if (it == m_paperdolls.end())
        return false;

    onDetachPaperdoll(*it);
    m_paperdolls.erase(it);

    return true;
}

bool Creature::detachPaperdollByPriority(uint8_t priority) {
    bool finded = false;
    for (auto it = m_paperdolls.begin(); it != m_paperdolls.end();) {
        const auto& obj = *it;
        if (obj->getPriority() == priority) {
            onDetachPaperdoll(obj);
            it = m_paperdolls.erase(it);
            finded = true;
        } else ++it;
    }

    return finded;
}

void Creature::onDetachPaperdoll(const PaperdollPtr& paperdoll) {
    paperdoll->callLuaField("onDetach", asLuaObject());
}

void Creature::clearPaperdolls() {
    for (const auto& e : m_paperdolls)
        onDetachPaperdoll(e);
    m_paperdolls.clear();
}

PaperdollPtr Creature::getPaperdollById(uint16_t id) {
    const auto it = std::find_if(m_paperdolls.begin(), m_paperdolls.end(),
                                 [id](const PaperdollPtr& obj) { return obj->getId() == id; });

    if (it == m_paperdolls.end())
        return nullptr;

    return *it;
}

void Creature::setPaperdollsDirection(Otc::Direction dir) const
{
    for (const auto& paperdoll : m_paperdolls) {
        if (paperdoll->m_thingType)
            paperdoll->m_direction = dir;
    }
}
