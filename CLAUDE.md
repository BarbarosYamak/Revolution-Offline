# Revolution Offline

## Mission

Recreate the gameplay experience of the Turkish Ultima Online shard
RevolutionUO, especially its Sphere-era mechanics, economy, skill
progression, PvP, crafting and player-driven world.

The project will also populate the server with autonomous simulated
players that obey the same rules as human players.

## Core Architecture

Server:
- SphereServer Source-X
- Sphere is the authoritative simulation.
- Do NOT replace Sphere with RunUO, ServUO or ModernUO.

Human client:
- Classic UO-compatible client using Revolution client data.

Bot:
- Headless real UO network client.
- Bots connect to Sphere exactly like normal players.
- Bots must NOT manipulate server state directly.

References:
- xrip/uo-client for headless protocol-client architecture.
- Klein187/uo-offline for behavioral/economic inspiration ONLY.
- Never port ModernUO server dependencies into our runtime.

## Core Principle

A bot must play the game.

It cannot:
- grant itself skills
- generate gold
- create items
- teleport without using game mechanics
- know global market data
- directly manipulate Sphere state

It must:
- train skills normally
- obey STR/DEX/INT
- obey the 700 skill cap
- acquire equipment normally
- gather/buy resources
- use reagents
- use player vendors
- suffer death and item loss
- use the same combat and magic rules as humans

## Character Design

Bots are not NPC professions.

Each bot is a persistent UO character with:

- aspiration
- target 700-point build
- target STR/DEX/INT
- economic strategy
- equipment goals
- personality
- friends/enemies
- market knowledge
- world knowledge
- play schedule

Example aspirations:

- Swordsman
- Fencer
- Macer
- Archer
- Pure Mage
- Warlock
- PK
- Tamer
- Treasure Hunter
- Fisher
- Miner
- Mage Blacksmith
- Full Crafter
- Tailor
- Merchant
- Alchemist/Scribe

Experienced characters pursue intentional completed builds.
Random incomplete skill distributions should primarily represent new
characters or characters currently retraining.

## Revolution Fidelity

Do NOT assume generic UO mechanics.

Every important mechanic should eventually be backed by:

1. Revolution official documentation
2. Revolution forum evidence
3. Revolution changelog evidence
4. Revolution client data
5. Sphere-era behavior

Especially verify:

- Magery
- precasting
- movement while casting
- fizzle/interruption
- Eval/Magery damage
- Meditation
- STR/DEX/INT
- Healing
- Poisoning
- weapon schools
- special attacks
- skill gains
- crafting
- loot
- gold economy
- taming
- robes
- armor restrictions
- vendors

If evidence is uncertain, mark it UNKNOWN instead of inventing behavior.

## Development Rule

First prove:

Sphere -> Headless Client -> Real Player Connection

before implementing sophisticated AI.

Do not prematurely add LLM runtime behavior.

Runtime priorities:

1. deterministic Sphere rules
2. deterministic tactical bot AI
3. utility/goal planning
4. persistent memory
5. optional LLM social/personality layer