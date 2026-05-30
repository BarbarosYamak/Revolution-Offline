'use strict';
// BankSkill — banking methods mixed into a bot prototype:
//     Object.assign(MyBot.prototype, BankSkill);
// Each method reads `this.token`; withdrawGold also reads `this.BANK` (the bank
// coords) and `this.WALLET` (default gold target), both defined by the bot.
//
// Auto-loaded with the other scripts/js/lib/*.js modules.
(function (g) {
    const BankSkill = {
        // Open a container by serial; resolves with the container_open event.
        async openContainer(serial) {
            const { token } = this;
            return token.retry(async () => {
                const [opened] = await token.wait(Promise.all([
                    waitForContainer({ serial, ms: 3000 }),
                    Player.use(serial),
                ]));
                return opened;
            }, 3, 500);
        },

        // Say "bank" and resolve with the bank box once it opens.
        async openBank() {
            const { token } = this;
            return token.retry(async () => {
                const [box] = await token.wait(Promise.all([
                    waitForContainer({ ms: 3000 }),
                    Player.say('bank'),
                ]));
                return box;
            }, 4, 1000);
        },

        // Drop one backpack stack into `container`, retrying past the deposit
        // cooldown ("must wait"). Returns true once it lands.
        async depositStack(item, container) {
            const { token } = this;
            return token.retry(async () => {
                await token.sleep(1000);
                Player.drop('0x' + item.serial.toString(16), container);
                let journal = null;
                try { journal = await token.wait(waitForJournal({ ms: 1200 })); }
                catch (error) { if (error === CANCELLED) throw error; journal = null; }
                if (journal && journal.toLowerCase().includes('must wait')) throw new Error('cooldown');
                return true;
            }, 5, 1500).catch((error) => { if (error === CANCELLED) throw error; return false; });
        },

        // Deposit every backpack stack whose name contains `nameFilter` into the
        // bank box. Assumes the bot is already within range of the bank.
        async deposit(nameFilter) {
            await this.openContainer(Player.equipment.backpack.serial);
            const bankBox = await this.openBank();
            const stacks = Player.equipment.backpack.items.filter((item) => item.name.includes(nameFilter));
            console.log(`[bank] deposit ${stacks.length} ${nameFilter} stack(s); weight ${Player.weight}`);
            for (const stack of stacks) await this.depositStack(stack, bankBox.serial);
            console.log(`[bank] deposited; weight ${Player.weight}`);
        },

        // Withdraw gold from the bank box until the backpack holds `amount`.
        async withdrawGold(amount = this.WALLET) {
            const { token } = this;
            const have = () => this.backpackCount(['gold']);
            if (have() >= amount) return;

            await this.walkTo(this.BANK, { range: 3 });

            for (let attempt = 0; attempt < 10 && have() < amount; attempt++) {
                token.check();
                let bankBox;
                try { bankBox = await this.openBank(); }
                catch (error) {
                    if (error === CANCELLED) throw error;
                    console.warn('[bank] bank did not open, retrying');
                    await token.sleep(1000);
                    continue;
                }

                const gold = Player.containerItems(bankBox.serial).find((item) => item.name.includes('gold'));
                if (!gold || gold.amount === 0) {
                    console.warn(`[bank] no gold in bank (have ${have()}, need ${amount})`);
                    break;
                }

                const take = Math.min(amount - have(), gold.amount);
                console.log(`[bank] taking ${take} gold (${have()} -> ${have() + take})`);
                await token.sleep(1000);
                Player.take(gold.serial, take);
                await token.sleep(1000);
            }
        },
    };

    g.BankSkill = BankSkill;
})(globalThis);
