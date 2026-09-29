'use strict';
// pk -- see lib/archetypes.js for the build, its source and evidence.
// Engine: lib/fighter.js. Pass options as the second argument, e.g.
//   new FighterBot('pk', { home: 'vesper', persona: { riskTolerance: 0.3, activeHours: [[18, 23]] } })
new FighterBot('pk').start();
