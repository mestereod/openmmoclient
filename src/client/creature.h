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

#pragma once

#include "outfit.h"
#include "thing.h"
#include <framework/core/declarations.h>
#include <framework/graphics/cachedtext.h>

#include "staticdata.h"

 // @bindclass
class Creature : public Thing
{
public:
    static double speedA;
    static double speedB;
    static double speedC;

    Creature();
    ~Creature() override;

    static bool hasSpeedFormula();

    void onCreate();

    void onAppear() override;
    void onDisappear() override;

    void draw(const Point& dest, bool drawThings = true, LightView* lightView = nullptr) override;
    void draw(const Rect& destRect, uint8_t size, bool center = false);
    void drawLight(const Point& dest, LightView* lightView) override;

    void internalDraw(Point dest, const Color& color = Color::white);
    void drawInformation(const MapPosInfo& mapRect, const Point& dest, int drawFlags);

    void setId(const uint32_t id) override { m_id = id; }
    void setMasterId(const uint32_t id) { m_masterId = id; }
    void setName(std::string_view name);
    void setHealthPercent(uint8_t healthPercent);
    void setManaPercent(uint8_t value) { m_manaPercent = value; }
    void setDirection(Otc::Direction direction);
    void setOutfit(const Outfit& outfit, bool fireEvent = true);
    void setLight(const Light& light) { m_light = light; }
    void setSpeed(uint16_t speed);
    void setBaseSpeed(uint16_t baseSpeed);
    void setSkull(uint8_t skull);
    void setShield(uint8_t shield);
    void setEmblem(uint8_t emblem);
    void setType(uint8_t type);
    void setIcon(uint8_t icon);
    void setIcons(const std::vector<std::tuple<uint8_t, uint8_t, uint16_t>>& icons);
    void setSkullTexture(const std::string& filename);
    void setShieldTexture(const std::string& filename, bool blink);
    void setEmblemTexture(const std::string& filename);
    void setTypeTexture(const std::string& filename);
    void setIconTexture(const std::string& filename);
    void setPassable(const bool passable) { m_passable = passable; }
    void setMountShader(std::string_view name);
    void setStaticWalking(uint16_t v);
    void setIconsTexture(const std::string& filename, const Rect& clip, const uint16_t count);

    void onStartAttachEffect(const AttachedEffectPtr& effect) override;
    void onDispatcherAttachEffect(const AttachedEffectPtr& effect) override;
    void onStartDetachEffect(const AttachedEffectPtr& effect) override;

    void addTimedSquare(uint8_t color);
    void removeTimedSquare() { m_showTimedSquare = false; }
    void showStaticSquare(const Color& color) { m_showStaticSquare = true; m_staticSquareColor = color; }
    void hideStaticSquare() { m_showStaticSquare = false; }

    void turn(Otc::Direction direction);
    void jump(int height, int duration);
    void allowAppearWalk() { m_allowAppearWalk = true; }

    bool isDrawingOutfitColor() const { return m_drawOutfitColor; }
    void setDrawOutfitColor(const bool draw) { m_drawOutfitColor = draw; }

    int getDisplacementX() const override;
    int getDisplacementY() const override;
    int getExactSize(int layer = 0, int xPattern = 0, int yPattern = 0, int zPattern = 0, int animationPhase = 0) override;

    uint8_t getSkull() { return m_skull; }
    uint8_t getShield() { return m_shield; }
    uint8_t getEmblem() { return m_emblem; }
    uint8_t getType() { return m_type; }
    uint8_t getIcon() { return m_icon; }
    uint8_t getHealthPercent() { return m_healthPercent; }
    uint8_t getManaPercent() { return m_manaPercent; }

    uint16_t getSpeed() { return m_speed; }
    uint16_t getBaseSpeed() { return m_baseSpeed; }
    uint16_t getStepDuration(bool ignoreDiagonal = false, Otc::Direction dir = Otc::InvalidDirection);

    uint32_t getId() override { return m_id; }
    uint32_t getMasterId() { return m_masterId; }
    std::string getName() { return m_name.getText(); }

    Point getDrawOffset() { return Point(-1, -1) * getDrawElevation() + getSubTileOffset(); }
    int getDrawElevation();

    Otc::Direction getDirection() { return m_direction; }
    const auto& getOutfit() { return m_outfit; }
    const Light& getLight() const override;
    bool hasLight() const override { return Thing::hasLight() || getLight().intensity > 0; }
    bool hasMountShader() const { return m_mountShaderId > 0; }

    Point getDisplacement() const override;
    PointF getJumpOffset() { return m_jumpOffset; }
    bool isTimedSquareVisible() { return m_showTimedSquare; }
    Color getTimedSquareColor() { return m_timedSquareColor; }
    bool isStaticSquareVisible() { return m_showStaticSquare; }
    Color getStaticSquareColor() { return m_staticSquareColor; }

    bool isPassable() const { return m_passable; }
    bool isMoving() { return m_subTileMoving || m_isPredicting || m_subTileTransitioning; }

    bool isRemoved() { return m_removed; }
    bool isRemoved() const { return m_removed; }
    const Position& getOldPosition() const { return m_oldPosition; }
    bool isInvisible() { return m_outfit.isEffect() && m_outfit.getAuxId() == 13; }
    bool isDead() { return m_healthPercent <= 0; }
    bool isFullHealth() { return m_healthPercent == 100; }
    bool canBeSeen() { return !isInvisible() || isPlayer(); }
    bool isCreature() const override { return true; }
    bool isCovered() { return m_isCovered; }

    void setCovered(bool covered);

    bool isDisabledWalkAnimation() { return m_disableWalkAnimation > 0; }
    void setDisableWalkAnimation(const bool v) {
        if (v) ++m_disableWalkAnimation; else {
            if (m_disableWalkAnimation <= 1) m_disableWalkAnimation = 0;
            else --m_disableWalkAnimation;
        }
    }

    void setTyping(bool typing);
    void sendTyping();
    bool getTyping() { return m_typing; }

    void setSubTilePosition(uint8_t subX, uint8_t subY);
    uint8_t getSubTileX() const { return m_subTileX; }
    uint8_t getSubTileY() const { return m_subTileY; }
    Point getSubTileOffset() const;

    // Client-side movement prediction
    void startMovementPrediction(Otc::Direction dir);
    void stopMovementPrediction();
    void rejectMovementPrediction();
    void resetContinuousMovementState();
    std::pair<float, float> getPredictedSubTileF() const;
    bool isMovementPredicting() const { return m_isPredicting; }
    Otc::Direction getPredictionDirection() const { return m_predictionDirection; }
    void clearPredictionCooldown() { m_predictionCooldown = false; }
    void setTypingIconTexture(const std::string& filename);
    void setBounce(const uint8_t minHeight, const uint8_t height, const uint16_t speed) {
        m_bounce = { .minHeight =
minHeight,
.height = height, .speed = speed
        };
    }

    void setWidgetInformation(const UIWidgetPtr& info);
    UIWidgetPtr getWidgetInformation() { return m_widgetInformation; }

    void setNameShader(const std::string& name) { m_nameShader = name; }
    std::string getNameShader() { return m_nameShader; }

    void setText(const std::string& text, const Color& color);
    std::string getText();
    void clearText() { setText("", Color::white); }
    bool canShoot(int distance);

    const auto& getIcons() {
        static std::vector<std::tuple<uint8_t, uint8_t, uint16_t>> vec;
        return m_icons ? m_icons->iconEntries : vec;
    }

    bool isCameraFollowing() const {
        return m_cameraFollowing;
    }

    void setCameraFollowing(bool v) {
        m_cameraFollowing = v;
    }

    void setVocation(uint8_t vocation) { m_vocation = vocation; }
    uint8_t getVocation() { return m_vocation; }

    void attachPaperdoll(const PaperdollPtr& obj);
    void clearPaperdolls();
    bool hasPaperdoll(uint16_t id);

    bool detachPaperdollById(uint16_t id);
    bool detachPaperdollByPriority(uint8_t priority);

    PaperdollPtr getPaperdollById(uint16_t id);

    const std::vector<PaperdollPtr>& getPaperdolls() { return m_paperdolls; };

protected:
    void setOldPositionSilently(const Position& pos) { m_oldPosition = pos; }
    void setRemovedSilently(const bool removed) { m_removed = removed; }

    void onDetachPaperdoll(const PaperdollPtr& paperdoll);
    void setPaperdollsDirection(Otc::Direction dir) const;

    ThingType* getThingType() const override;
    ThingType* getMountThingType() const;

    void onDeath();
    void onPositionChange(const Position& newPos, const Position& oldPos) override;

    Otc::Direction m_direction{ Otc::South };

private:
    void updateJump();
    void updateShield();
    void updateWalkAnimation();
    void startSubTileTransition(Otc::Direction dir);
    bool checkNextTileWalkable(Otc::Direction dir) const;

    uint16_t getCurrentAnimationPhase(bool mount = false);

    struct CachedStep
    {
        uint16_t speed{ 0 };
        uint16_t groundSpeed{ 0 };

        uint16_t duration{ 0 };
        uint16_t walkDuration{ 0 };
        uint16_t diagonalDuration{ 0 };

        uint16_t getDuration(const Otc::Direction dir) const { return Position::isDiagonal(dir) ? diagonalDuration : duration; }
    };

    struct IconRenderData
    {
        struct AtlasIconGroup
        {
            TexturePtr texture;
            Rect clip;
            uint16_t count{ 0 };
        };

        std::vector<AtlasIconGroup> atlasGroups;
        std::vector<std::tuple<uint8_t, uint8_t, uint16_t>> iconEntries; // (icon, category, count)
        CachedText numberText;
    };

    std::vector<PaperdollPtr> m_paperdolls;

    UIWidgetPtr m_widgetInformation;

    std::unique_ptr<IconRenderData> m_icons;

    TexturePtr m_skullTexture;
    TexturePtr m_shieldTexture;
    TexturePtr m_emblemTexture;
    TexturePtr m_typeTexture;
    TexturePtr m_iconTexture;
    TexturePtr m_typingIconTexture;

    ScheduledEventPtr m_outfitColorUpdateEvent;

    EventPtr m_disappearEvent;

    CachedText m_name;
    std::string m_nameShader;
    CachedStep m_stepCache;

    Position m_oldPosition;

    Timer m_footTimer;
    Timer m_outfitColorTimer;

    Outfit m_outfit;
    Light m_light;

    Color m_timedSquareColor{ Color::white };
    Color m_staticSquareColor{ Color::white };
    Color m_informationColor{ Color::white };

    Bounce m_bounce;

    // jump related
    Timer m_jumpTimer;
    PointF m_jumpOffset;
    float m_jumpHeight{ 0 };
    float m_jumpDuration{ 0 };

    uint32_t m_id{ 0 };
    uint32_t m_masterId{ 0 };

    uint16_t m_calculatedStepSpeed{ 0 };
    uint16_t m_speed{ 0 };
    uint16_t m_baseSpeed{ 0 };
    uint16_t m_walkingAnimationSpeed{ 0 };

    uint8_t m_type;
    uint8_t m_healthPercent{ 101 };
    uint8_t m_manaPercent{ 101 };
    uint8_t m_skull{ Otc::SkullNone };
    uint8_t m_icon{ Otc::NpcIconNone };
    uint8_t m_shield{ Otc::ShieldNone };
    uint8_t m_emblem{ Otc::EmblemNone };

    uint8_t m_walkAnimationPhase{ 0 };

    // sub-tile position (0-255 subdivisions within a tile)
    uint8_t m_subTileX{ 128 };
    uint8_t m_subTileY{ 128 };

    Timer m_subTileMoveTimer;
    bool m_subTileMoving{ false };

    // Client-side movement prediction fields
    Otc::Direction m_predictionDirection{ Otc::InvalidDirection };
    bool m_isPredicting{ false };
    Timer m_predictionTimer;
    uint16_t m_predictionStepDuration{ 0 };

    // Whether the next tile in the prediction direction is walkable (client-side collision)
    bool m_predictionNextTileWalkable{ true };

    // Post-prediction cooldown: ignore stale server sub-tile updates
    Timer m_predictionCooldownTimer;
    bool m_predictionCooldown{ false };

    // Collision suppression: after cancelWalk, suppress visual prediction
    // in the same direction to prevent flickering
    bool m_collisionSuppressed{ false };
    Otc::Direction m_collisionDirection{ Otc::InvalidDirection };

    // Sub-tile transition: smooth interpolation from tile edge to center for non-predicting creatures
    bool m_subTileTransitioning{ false };
    Timer m_subTileTransitionTimer;
    uint16_t m_subTileTransitionDuration{ 0 };
    float m_subTileTransitionStartX{ 128.0f };
    float m_subTileTransitionStartY{ 128.0f };

    uint8_t m_exactSize{ 0 };

    uint8_t m_disableWalkAnimation{ 0 };

    // Mount Shader
    uint8_t m_mountShaderId{ 0 };

    Otc::Direction m_lastStepDirection{ Otc::InvalidDirection };

    bool m_shieldBlink{ false };
    bool m_passable{ false };
    bool m_allowAppearWalk{ false };
    bool m_showTimedSquare{ false };
    bool m_showStaticSquare{ false };
    bool m_cameraFollowing{ false };

    bool m_removed{ true };
    bool m_drawOutfitColor{ true };
    bool m_showShieldTexture{ true };
    bool m_typing{ false };
    bool m_isCovered{ false };

    StaticTextPtr m_text;

    uint8_t m_vocation{ 0 };
};

// @bindclass
class Npc final : public Creature
{
public:
    bool isNpc() const override { return true; }
};

// @bindclass
class Monster final : public Creature
{
public:
    bool isMonster() const override { return true; }
};
