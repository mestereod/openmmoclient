# Resumo do projeto: Chronicles of the Living World

**Chronicles of the Living World** é um MMORPG no estilo Isekai (inspirado em Solo Leveling), em fase de MVP com foco em fundação técnica e combate visceral.

---

## Conceito central

**Mundo vivo:** O jogo é pensado como um ecossistema persistente onde o mundo reage às ações do jogador — persistência, economia, consequências em cascata ("efeito borboleta"). Eventos de combate, destruição de estruturas e morte de chefes alteram o estado persistido do mapa no Canary, impactando todos os jogadores conectados.

**Cidadão:** O jogador é uma entidade persistente no Canary via MariaDB; progresso, inventário, reputação e interações são salvos e influenciam o mundo. O servidor mantém um registro de ações significativas por jogador que alimentam o sistema de consequências globais.

**Loop principal:** Portais (Gates) e masmorras instanciadas, gerenciadas pelo Canary como áreas temporárias com spawn próprio. A progressão do jogador se reflete visualmente no mundo via estados de tile, monstros com shaders de tier e efeitos atmosféricos enviados pelo servidor.

---

## Combate e imersão

**Gore e desmembramento visual:** Quando uma criatura morre de forma violenta, o Canary envia um opcode customizado (`Opcode_ViolentDeath`) com o ângulo e tipo do golpe. O OTClient traduz isso em:
- **Attached Effects** — partes do corpo (APNG animados) que se desprendem com offset direcional configurado por `dirOffset` no `AttachedEffectManager`
- **ParticleSystem** — emissores de sangue com velocidade, gravidade e tempo de vida, lançados na direção do golpe
- **Shader de dano progressivo** — sobreposição de textura de feridas na criatura via GLSL (OpenGL) ou HLSL (DirectX 9/11/12), uniforme `u_DamageProgress` enviado incrementalmente pelo servidor a cada hit

**Estética anime:** Sprites customizados com outline shader (pós-processo de borda nítida via framebuffer) e toon shader configurado em `modules/game_shaders/`. O sistema de Outfit + Paperdoll do OTClient permite compor visuais por camadas (corpo, arma, armadura, aura), tudo controlado via Lua.

**Feedback de impacto:**
- *Hit-flash*: shader de inversão de cor disparado por 1–2 frames ao receber dano (GLSL/HLSL no `ShaderManager` por `OutfitId`, compatível com todos os backends)
- *Camera shake*: translação rápida do `MapView` via Lua ao receber ou causar dano crítico
- *Partículas de sangue*: `ParticleSystem` com emissores curtos e intensos no tile do impacto

**Rede e autoridade:** Toda lógica de dano, morte e desmembramento é calculada e validada no Canary. O OTClient recebe apenas pacotes de resultado e renderiza os efeitos visuais correspondentes — nunca simula dano localmente sem confirmação do servidor.

---

## Stack tecnológica (MVP)

| Área | Tecnologia |
|---|---|
| **Cliente** | OTClient — Redemption (C++23 + Lua) |
| **Plataformas** | Windows, Linux, Android, WebAssembly (browser) |
| **Render backends** | OpenGL / OpenGL ES 2.0 · DirectX 9 · DirectX 11 · DirectX 12 |
| **Servidor** | Canary (C++, autoritativo) |
| **Banco de dados** | MariaDB (Docker) |
| **Protocolo** | TCP/HTTP/WS com opcodes customizados |
| **RPG / Stats** | Lua scripts no Canary (vocações, fórmulas de combate, percepção) |
| **Animação** | APNG sprites + sistema de Attached Effects do OTClient |
| **Personagens** | Sprite sheets customizados + sistema Outfit/Paperdoll |
| **Gore / Efeitos** | AttachedEffectManager + ParticleSystem + GLSL/HLSL shaders de dano |
| **Otimização** | DrawPool multi-thread + Texture Atlas (em desenvolvimento) |

---

## Estrutura do projeto

```
canary/
  src/
    game/         — lógica de combate, Gates, mundo vivo
    map/          — tiles destrutíveis, estado persistente do mapa
    creatures/    — criaturas com tier, HP de estrutura, comportamento
  data/
    scripts/      — Lua server-side: eventos, Gates, recompensas
    world/        — mapas, spawns, instâncias de masmorra

openmmoclient/
  src/
    client/       — protocolo, tile, criatura, efeitos (C++)
  modules/
    game_gore/        — AttachedEffects de partes do corpo, emissores de sangue
    game_shaders/     — toon shader, outline, hit-flash, dano progressivo
    game_gates/       — UI e lógica client-side de Portais
    game_worldstate/  — exibição de estados globais do mundo vivo
```

---

## Próximos passos imediatos (MVP)

1. **Canary:** Definir e registrar os opcodes customizados (`Opcode_ViolentDeath`, `Opcode_TileDestruction`, `Opcode_GateState`, `Opcode_WorldEvent`) no protocolo cliente-servidor.
2. **OTClient:** Criar o módulo `game_gore` — registrar os Attached Effects de partes do corpo no `AttachedEffectManager` com `dirOffset` por direção do golpe e emissores de `ParticleSystem` de sangue.
3. **OTClient:** Implementar o shader de dano progressivo em `modules/game_shaders/` (fragment shader com `u_DamageProgress` e textura de feridas sobreposta ao outfit).
4. **Canary:** Implementar o sistema de Gates como instâncias de dungeon — criação de área temporária, spawn de monstros por tier, lógica de entrada/saída e timer de encerramento.
5. **Canary + OTClient:** Implementar o primeiro ciclo de "mundo vivo" — morte de chefe altera tile do mapa (estrutura destruída), servidor persiste o estado e notifica todos os clientes via `Opcode_TileDestruction`.

---

## Regras de desenvolvimento

- **Canary é autoridade absoluta** — dano, morte, desmembramento e estado do mundo são calculados e validados server-side; o cliente apenas renderiza o resultado.
- **Attached Effects para gore** — partes do corpo, auras e efeitos de impacto usam o `AttachedEffectManager` com APNG; nunca simular visualmente sem receber o pacote do servidor.
- **Lua limpo no cliente** — callbacks de protocolo disparam efeitos visuais (`game_gore`, `game_shaders`); lógica de jogo fica exclusivamente no Canary.
- **Opcodes semânticos** — cada evento de jogo relevante (golpe crítico, destruição, entrada em Gate) tem opcode próprio; evitar reutilizar opcodes genéricos de item/tile para eventos de combate.
- **Layers de colisão via tile flags** — detecção de golpes e projéteis usa os flags de tile do Canary (`TILESTATE_*`) e não lógica client-side.

---

Em resumo, o MVP visa estabelecer o protocolo customizado entre Canary e OTClient, implementar o primeiro ciclo de combate com gore visual (Attached Effects + partículas + shaders), e ter um Gate funcional com destruição de tile persistida — como fundação para o "mundo vivo" e o combate visceral do jogo.
