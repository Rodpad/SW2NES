// Host integration: actual SDK browser renderer + QuickNES, mocked console IO.
// Usage: node bench/native-ui-test.cjs /path/to/extracted/civhomebrew-sdk-1.3.0
// No game ROMs or device connection required.
const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const path = require('node:path');
const {createServer} = require('node:http');
const sdk = path.resolve(process.argv[2] || '');
const {open, sleep} = require(path.join(sdk, 'testing/chrome.cjs'));
const root = path.resolve(__dirname, '..');
const fixture = new Uint8Array(16 + 16384 + 8192);
fixture.set([0x4e, 0x45, 0x53, 0x1a, 1, 1]);
// NROM: set the universal background colour, enable rendering, then loop.
fixture.set([0x78,0xd8,0xa2,0xff,0x9a,0xe8,0x8e,0,0x20,0x8e,1,0x20,0xad,2,0x20,
  0xa9,0x3f,0x8d,6,0x20,0xa9,0,0x8d,6,0x20,0xa9,0x21,0x8d,7,0x20,
  0xa9,0x0a,0x8d,1,0x20,0x4c,0x23,0x80], 16);
fixture.set([0,0x80,0,0x80,0,0x80], 16 + 16384 - 6);
const html = `<!doctype html><style>html,body{margin:0;width:100%;height:100%;overflow:hidden}</style><body>
<script src="/sdk.js"></script><script src="/bench/native-mock.js"></script><script>
window.ready = (async function () {
  document.createRange = undefined; window.MutationObserver = undefined;
  window.session = await CivAppsNativeUI.sessions.start({browser:true});
  session.close = function () { throw Error('App must not close the shared session'); };
  var mountView = CivHomebrewSDK.app.mount;
  CivHomebrewSDK.app.mount = async function(root, options) {
    var view = await mountView(root, options), close = view.close;
    view.close = async function() { if(window.closeGate)await closeGate;await close(); };
    return view;
  };
  window.source = (await (await fetch('/dist/sw2nes.civapp')).json()).scripts.map(s=>s.source).join('\\n;\\n');
  __mock.dirs.add('save:/nes'); __mock.dirs.add('save:/civhomebrew');
  __mock.dirs.add('save:/civhomebrew/data'); __mock.dirs.add('save:/civhomebrew/data/sw2nes');
  __mock.files.set('save:/nes/Fixture.nes',new Uint8Array(await (await fetch('/fixture')).arrayBuffer()));
  __mock.files.set('save:/civhomebrew/data/sw2nes/settings.json',new TextEncoder().encode(JSON.stringify({v:6,crt:5,statsLevel:3})));
  window.launch = function () { window.done=false; window.result=(0,eval)(source); result.then(()=>done=true); };
  launch();
})();</script>`;
const server = createServer(async (req, res) => {
  try {
    const url = new URL(req.url, 'http://localhost');
    if (url.pathname === '/') return res.end(html);
    if (url.pathname === '/fixture') return res.end(fixture);
    if (url.pathname === '/sdk.js') return res.end(await fs.readFile(path.join(sdk, 'civapps-native-ui.js')));
    const file = path.resolve(root, '.' + url.pathname);
    if (!file.startsWith(root + path.sep)) throw Error('Invalid path');
    res.end(await fs.readFile(file));
  } catch (error) { res.statusCode = 404; res.end(error.message); }
});
(async function () {
  let browser;
  try {
    await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
    browser = await open();
    const {evaluate: ev, until} = browser;
    await browser.call('Emulation.setDeviceMetricsOverride', {width:1280,height:720,deviceScaleFactor:1,mobile:false});
    await browser.call('Page.navigate', {url:'http://127.0.0.1:' + server.address().port});
    await until('!!window.ready');
    await ev('ready');
    await until('window.__civswitchApp && session.active && __civswitchApp.getState().roms === 1');
    const press = async buttons => { await ev('__mock.buttons=' + buttons); await sleep(130); await ev('__mock.buttons=0'); await sleep(130); };
    let framebuffer;
    for (let cycle = 0; cycle < 3; cycle++) {
      const [width,height] = [[1280,720],[960,540],[1920,1080]][cycle];
      await browser.call('Emulation.setDeviceMetricsOverride', {width,height,deviceScaleFactor:1,mobile:false});
      assert.equal(await ev('done'), false, 'evaluation must retain the app lifetime');
      await press(1);
      await until('__civswitchApp.getState().frames > 8');
      await until('session.__sw2nesGraph && session.__sw2nesGraph.bytes.some((v,i)=>i%4===1&&v===226)');
      const state = await ev(`({mode:__civswitchApp.getState().mode,
        pixels:document.querySelector('#sw2nes canvas[data-civ-pixels]').getAttribute('data-civ-pixels'),
        images:session.active.lease.scene.commands.filter(c=>c.kind==='image').length,
        graph:session.active.lease.scene.commands.some(c=>c.kind==='image'&&c.source===session.__sw2nesGraph.image.source),
        pixelSlots:[...session.geometry.textures.values()].filter(t=>t.type===3).length,
        errors:session.stats.errors,legacy:__mock.presents})`);
      assert.equal(state.mode, 'running');
      assert.equal(state.legacy, 0, 'native mode must not call the Gameface live-image provider');
      assert.deepEqual(state.errors, []);
      assert.ok(state.images >= 4, 'NES framebuffer, both CRT strips and graph must render');
      assert.ok(state.graph, 'native graph must render as a retained pixel image');
      assert.equal(state.pixelSlots, 2, 'relaunch retains one NES image and one graph image');
      const frames = await ev('__civswitchApp.getState().frames');
      await ev(`Object.defineProperty(document.getElementById('sw2nes'),'innerHTML',{configurable:true,get(){throw Error('Do not serialize the DOM during framebuffer updates');}});`);
      await until('__civswitchApp.getState().frames > '+(frames+4));
      if (cycle === 0) {
        const shot = await browser.call('Page.captureScreenshot', {format:'png'});
        await fs.writeFile(path.join(root, 'dist/native-ui-game.png'), Buffer.from(shot.data, 'base64'));
      }
      if (framebuffer) assert.equal(state.pixels, framebuffer, 'relaunch must reuse the retained pixel slot');
      framebuffer = state.pixels;
      await press(64 | 128);
      await until('__civswitchApp.getState().mode === "menu"');
      assert.ok(await ev('session.active.lease.scene.commands.some(c=>c.kind==="text"&&c.text.indexOf("Resume")>=0)'), 'Npad-only menu update must be visible');
      if (cycle === 0) {
        const shot = await browser.call('Page.captureScreenshot', {format:'png'});
        await fs.writeFile(path.join(root, 'dist/native-ui-menu.png'), Buffer.from(shot.data, 'base64'));
      }
      // Native rendering must not depend on the layout engine scrolling.
      await ev(`window.menuPanel=[...document.querySelectorAll('#sw2nes div')].find(n=>n.children.length===0&&n.textContent==='Resume').parentNode.parentNode;
        Object.defineProperty(menuPanel,'scrollTop',{configurable:true,get:()=>0,set:()=>{}});__mock.buttons=8192;`);
      await until(`[...document.querySelectorAll('#sw2nes div')].some(n=>n.style.borderLeftColor==='rgb(228, 70, 59)'&&n.firstElementChild?.textContent==='Quit game')`);
      await ev('__mock.buttons=0;new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve)))');
      const quit = await ev(`(()=>{const n=[...menuPanel.children].find(n=>n.firstElementChild?.textContent==='Quit game'),r=n.getBoundingClientRect(),p=menuPanel.getBoundingClientRect(),c=session.active.lease.scene.commands.find(c=>c.kind==='text'&&c.text==='Quit game');return {row:[r.top,r.bottom],panel:[p.top,p.bottom],text:c&&{baseline:c.baseline,clip:c.clip}};})()`);
      assert.ok(quit.row[0]>=quit.panel[0]&&quit.row[1]<=quit.panel[1]&&quit.panel[0]>=0&&quit.panel[1]<=height, 'Quit game stays inside the menu at '+width+'x'+height+': '+JSON.stringify(quit));
      assert.ok(quit.text&&quit.text.baseline>quit.text.clip[1]&&quit.text.baseline<quit.text.clip[3], 'Quit game is visible in native output');
      await press(1);
      await until('__civswitchApp.getState().mode === "browser"');
      // Lifetime cannot resolve while the view is still releasing its work.
      await ev(`window.oldApp=__civswitchApp;
        window.releaseClose=null;window.closeGate=new Promise(r=>releaseClose=r);
        window.closing=oldApp.close();void 0;`);
      assert.equal(await ev('done'), false);
      await ev('releaseClose();closing');
      assert.equal(await ev('done && !window.__civswitchApp && !session.active && !document.getElementById("sw2nes")'), true);
      assert.equal(await ev('CivAppsNativeUI.sessions.current === session'), true);
      assert.deepEqual(await ev('__mock.errors'), []);
      if (cycle < 2) { await ev('launch()'); await until('window.__civswitchApp && session.active && __civswitchApp.getState().roms === 1'); }
    }
    // Closing while attach is pending also releases its eventual view.
    await ev(`window.mount=CivHomebrewSDK.app.mount;window.releaseMount=null;
      CivHomebrewSDK.app.mount=async function(root,options){await new Promise(r=>releaseMount=r);return mount(root,options);};launch();`);
    await until('!!window.releaseMount');
    await ev('window.earlyClose=__civswitchApp.close();releaseMount();earlyClose');
    assert.equal(await ev('done && !session.active && !window.__civswitchApp'), true);
    // The same package must still use the legacy live texture without a session.
    await ev(`CivHomebrewSDK.app.mount=mount;window.CivAppsNativeUI=null;launch();`);
    await until('window.__civswitchApp && __civswitchApp.getState().roms === 1');
    await press(1);
    await until('__mock.presents > 0');
    await ev('__civswitchApp.close();result');
    assert.equal(await ev('done && !window.__civswitchApp'), true);
    assert.deepEqual(await ev('__mock.errors'), []);
    console.log('PASS: three shared-renderer cycles, CRT/graph, visible Quit without scrolling at 540p/720p/1080p, close ordering, pending attach, legacy texture fallback');
  } finally {
    if (browser) await browser.close();
    server.close();
  }
})().catch(error => { console.error(error); process.exitCode = 1; });
