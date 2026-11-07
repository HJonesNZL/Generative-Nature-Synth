#pragma once
namespace atlas::standalone {
inline constexpr const char *pieceWorkflowSmoke = R"JS(
   const pieceButton = text => [...document.querySelectorAll('button')].find(button => button.textContent.trim() === text);
   const pollPiece = async (read, predicate, description) => {
     const deadline = Date.now() + 30000;
     while (true) {
       const value = await read();
       if (predicate(value)) return value;
       if (Date.now() > deadline) throw Error(`Timed out waiting for ${description}: ${JSON.stringify(value)}`);
       await new Promise(resolve => setTimeout(resolve, 100));
     }
   };
   await (async () => {
   const wholeId = 'd774207b-342c-49bd-a328-5649f9de84ce';
   const fiveId = '980d87e8-27f1-454d-b4df-5a3a7c13388d';
   const managed = await call('getLiveSources');
   if (!managed.libraryActive) {
     const sessions = await call('listSessions', {limit: 100});
     const source = sessions.find(session => session.id === fiveId && session.eventCount === 5);
     if (!source) throw Error('Missing genuine five-observation historical fixture.');
     pieceButton('Replay').click();
     if (WHOLE_INTERVAL_ONLY) {
       const whole = sessions.find(session => session.id === wholeId && session.eventCount === 224);
       if (!whole) throw Error('Missing verified whole-deployment historical fixture.');
       await waitFor(() => [...document.querySelectorAll('.session-row')].some(row => row.innerText.includes(whole.name)), 'whole deployment row');
       [...document.querySelectorAll('.session-row')].find(row => row.innerText.includes(whole.name)).click();
       await waitFor(() => document.querySelector('.session-row[aria-pressed="true"]')?.innerText.includes(whole.name), 'whole deployment highlight');
       await waitFor(() => pieceButton('Observed'), 'timeline basis controls');
       pieceButton('Observed').click();
       await waitFor(() => [...document.querySelectorAll('button')].some(button => button.textContent.startsWith('Fit full interval') && !button.disabled), 'Fit full interval control');
       const fit = [...document.querySelectorAll('button')].find(button => button.textContent.startsWith('Fit full interval'));
       fit.click();
       const preparation = await pollPiece(() => call('getTransport'), value => !!value.preparationId, 'whole-deployment observed replay preparation');
       const observed = [...document.querySelectorAll('[aria-label="Timeline basis"] button')].find(button => button.textContent === 'Observed');
       if (observed?.getAttribute('aria-checked') !== 'true' || BigInt(preparation.durationUs) <= 0n || BigInt(preparation.durationUs) > 1800000000n) throw Error('Whole observed interval was not prepared at a bounded fitted speed.');
       result.wholeDeployment = {id:whole.id, eventCount:whole.eventCount, highlighted:true, basis:'observed', fitLabel:fit.textContent, preparation};
       return;
     }
     await waitFor(() => [...document.querySelectorAll('.session-row')].some(row => row.innerText.includes(source.name)), 'external historical piece');
     [...document.querySelectorAll('.session-row')].find(row => row.innerText.includes(source.name)).click();
     await waitFor(() => [...document.querySelectorAll('[role="radiogroup"][aria-label="Replay speed"] button')].find(button => button.textContent.startsWith('240')), 'Replay speed controls');
     [...document.querySelectorAll('[role="radiogroup"][aria-label="Replay speed"] button')].find(button => button.textContent.startsWith('240')).click();
     await pollPiece(() => call('getTransport'), transport => !!transport.preparationId, 'external replay preparation');
     document.querySelector('[aria-label="Project"]').click();
     await waitFor(() => pieceButton('Import selected piece to managed library') && !pieceButton('Import selected piece to managed library').disabled, 'real import button');
     pieceButton('Import selected piece to managed library').click();
     const imported = await pollPiece(() => call('getPieceJob'), job => job && job.status !== 'running', 'native import');
     if (imported.status !== 'completed' || imported.piece.eventCount !== 5) throw Error('Native import did not complete: ' + JSON.stringify(imported));
     await waitFor(() => pieceButton('Open saved piece') && !pieceButton('Open saved piece').disabled, 'Open saved piece');
     pieceButton('Open saved piece').click();
     return; // The production UI switches managed library and reloads.
   }
   const persisted = {sourceId:fiveId, imported: await call('getPieceJob')};
   if (persisted.imported?.piece?.id !== fiveId || managed.collecting) throw Error('Managed reload lost import context or enabled collection.');
   await waitFor(() => [...document.querySelectorAll('[role="radiogroup"][aria-label="Replay speed"] button')].find(button => button.textContent.startsWith('240')), 'managed Replay speed controls');
   [...document.querySelectorAll('[role="radiogroup"][aria-label="Replay speed"] button')].find(button => button.textContent.startsWith('240')).click();
   const transport = await pollPiece(() => call('getTransport'), value => !!value.preparationId, 'managed React replay preparation after reload');
   const sessions = await call('listSessions', {limit:100});
   const imported = sessions.find(session => session.id === persisted.imported.piece.id);
   if (!imported || imported.eventCount !== 5) throw Error('Managed library lost the imported piece.');
   await waitFor(() => document.querySelector('.session-row[aria-pressed="true"]')?.innerText.includes(imported.name), 'React selected imported managed piece');
   pieceButton('Arrival').click();
   await pollPiece(() => call('getTransport'), value => !!value.preparationId && value.preparationId !== transport.preparationId, 'explicit arrival preparation for bounded offline export');
   document.querySelector('[aria-label="Project"]').click();
   await waitFor(() => { const field = document.querySelector('input[aria-label="Soundscape name"]'); return field && !field.disabled; }, 'soundscape name');
   const input = document.querySelector('input[aria-label="Soundscape name"]');
   const setter = Object.getOwnPropertyDescriptor(HTMLInputElement.prototype,'value').set;
   setter.call(input,'Managed historical smoke'); input.dispatchEvent(new Event('input',{bubbles:true}));
   await waitFor(() => pieceButton('Save soundscape') && !pieceButton('Save soundscape').disabled, 'Save soundscape');
   pieceButton('Save soundscape').click();
   await waitFor(() => pieceButton('Export saved WAV') && !pieceButton('Export saved WAV').disabled, 'saved immutable soundscape');
   const saved = (await call('listSoundscapes',{limit:100})).items.find(scene => scene.name === 'Managed historical smoke');
   if (!saved || !saved.valid || saved.eventCount !== 5) throw Error('Saved soundscape does not retain imported material.');
   pieceButton('Export saved WAV').click();
   await pollPiece(() => call('getAudioState'), () => !!document.querySelector('a[download][data-artifact-id]'), 'completed saved WAV');
   const link = document.querySelector('a[download][data-artifact-id]');
   if (!link || !/^[0-9a-f]{32}$/.test(link.dataset.artifactId)) throw Error('No opaque WAV export artifact.');
   result.pieceWorkflow = {sourceId:persisted.sourceId, imported:persisted.imported, managed:true, collecting:managed.collecting, transport, saved, artifactId:link.dataset.artifactId};
   link.click();
   })();
   if (!WHOLE_INTERVAL_ONLY && !result.pieceWorkflow) return;
)JS";
}
