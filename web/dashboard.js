/* UltraLift's local display/request surface. Motion remains in the A16 firmware. */
(() => {
  'use strict';
  const states = new Map(), names = new Map(), rows = new Map();
  const pending = new Set();
  let connected = false, lastEvent = 0, scheduled = false, scheduledAt = 0, generation = 0, stopBusy = false, events = null;
  const $ = selector => document.querySelector(selector);
  const named = (domain, name) => states.get(names.get(`${domain}/${name}`));
  const finite = value => value === null || value === undefined || value === '' ? null : Number.isFinite(Number(value)) ? Number(value) : null;
  const number = (domain, name) => finite(named(domain, name)?.value);
  const text = name => named('text_sensor', name)?.state ?? '—';
  const flag = (domain, name) => {
    const data = named(domain, name);
    if (!data || data.value === null || data.state === 'NA') return null;
    return data.value === true || data.value === 1 || data.state === 'ON';
  };
  const online = () => connected && Date.now() - lastEvent < 15000;
  const benchName = 'Bench Test (FSM off)';
  const testing = () => flag('switch', benchName) === true;
  const isCalibration = data => data.domain === 'number' && /^Cal .*Angle|^Cal Angle/.test(data.name);
  const isRelay = data => data.domain === 'switch' && data.name.endsWith('— bench only');
  const escape = value => String(value).replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
  document.title = 'UltraLift';
  if (!$('meta[name="viewport"]')) {
    const meta = document.createElement('meta'); meta.name = 'viewport'; meta.content = 'width=device-width, initial-scale=1'; document.head.append(meta);
  }
  document.querySelector('esp-app')?.remove();
  document.body.insertAdjacentHTML('beforeend', `
    <header class="masthead"><div class="brand"><svg viewBox="0 0 40 40" aria-hidden="true"><path d="M5 23h30l-6 9H12zM12 20V9h16v11M6 36q7-5 14 0q7-5 14 0"/></svg><div><h1>UltraLift</h1><span>At the dock · Local control</span></div></div><span id="connection" class="badge">Connecting…</span></header>
    <div class="command-bar" role="group" aria-label="Lift controls"><div class="destinations">${[['Lift','Park the boat'],['Ready','Boat on bunks'],['Guide','Floating, centered'],['Lower','Fully lowered']].map(([name,hint])=>`<button data-command="${name}" disabled><strong>${name}</strong><span>${hint}</span></button>`).join('')}</div><button id="stop" class="stop" data-command="Stop" disabled><span class="stop-square" aria-hidden="true"></span>Stop</button></div>
    <main>
      <section class="panel overview" aria-labelledby="headline"><div class="heading-row"><div><span class="eyebrow">Lift status</span><h2 id="headline">Connecting to the lift…</h2></div><div class="heading-meta"><div class="water-status"><span class="eyebrow">Water temperature</span><strong id="temperature">—</strong></div><span id="position" class="position">—</span></div></div>
        <p id="connection-help" class="help">Waiting for live controller data.</p><div id="notice" class="notice" role="status" hidden></div>
        <div class="overview-grid">
          <article class="height-card"><span class="eyebrow">Lift height</span><strong id="height" class="big-value">—</strong><p id="bunk-height">Bunk height —</p><div class="height-track" aria-hidden="true"><span id="height-fill"></span></div><div class="scale"><span>Lowered</span><span>Lift</span></div></article>
          <article><span class="eyebrow">Level</span><strong id="level">—</strong><p id="level-detail">Waiting for both sensors</p></article>
          <article><span class="eyebrow">Operations</span><dl class="operations"><div><dt title="Commanded blower output">Blower</dt><dd id="blower">—</dd></div><div><dt>Starboard valve</dt><dd id="starboard-valve">—</dd></div><div><dt>Port valve</dt><dd id="port-valve">—</dd></div></dl><p class="detail">Valves show end-stop feedback</p></article>
        </div>
        <div class="info-strip"><span>Boat <strong id="boat">—</strong></span><span>Last stop <strong id="last-stop">—</strong></span></div>
      </section>
      <div id="message" role="status" aria-live="polite" hidden></div>
      <section class="panel maintenance"><div><span class="eyebrow">Automatic maintenance</span><p id="maintenance-status">—</p></div><div class="toggles"><button data-toggle="Auto-Maintain Height" disabled>Height · —</button><button data-toggle="Auto-Maintain Level" disabled>Level · —</button></div></section>
      <details class="panel" id="diagnostics"><summary>Diagnostics <span>Readings & recent activity</span></summary><div id="diagnostic-rows" class="readings"></div></details>
      <details class="panel" id="advanced"><summary>Advanced <span>Calibration, tuning & service</span></summary><p class="help">Your existing calibration stays in place. These controls are for occasional adjustments and service.</p>
        <details class="subsection" id="calibration"><summary>Calibration</summary><p class="help">Capture only when the lift is parked at the intended position. Each capture replaces that side’s saved position.</p><p id="calibration-summary" class="calibration-summary">—</p><div id="capture-controls" class="capture-grid"></div><div id="calibration-numbers" class="settings-grid"></div></details>
        <details class="subsection" id="tuning"><summary>Tuning</summary><div id="tuning-numbers" class="settings-grid"></div></details>
        <details class="subsection" id="service"><summary>Service & bench</summary><p class="help">Bypass opens both valves and turns the blower off. Bench suspends automatic control for direct output testing. Stop remains available.</p><div id="service-controls" class="service-grid"></div><div id="bench-controls" class="service-grid"></div><p class="help">Inject level fail exercises the emergency response and can move the lift.</p><div id="test-controls" class="service-grid"></div><p class="help">Occasional move. Inches match Bunk Height. Leave Bench Test off — Go uses the normal controller.</p><form id="height-goto" class="height-goto"><label>Target bunk height<span class="field-unit">inches from the waterline</span><input id="height-target" type="number" inputmode="decimal" autocomplete="off" required aria-label="Target bunk height in inches"></label><button type="submit" id="height-go" disabled>Go</button></form></details>
      </details>
      <footer><span>Runs on the A16 · No Home Assistant connection required</span><span id="uptime">—</span></footer>
    </main>`);

  function message(value, error = false) {
    $('#message').textContent = value; $('#message').hidden = !value; $('#message').className = error ? 'error' : '';
  }
  function scheduleRender() {
    const now = Date.now();
    if (scheduled && now - scheduledAt < 2000) return;
    scheduled = true; scheduledAt = now;
    const paint = () => { scheduled = false; render(); };
    if (document.hidden) setTimeout(paint, 200);
    else requestAnimationFrame(paint);
  }
  function addRow(data) {
    if (!data.name || rows.has(data.id)) return;
    if (data.name === 'Target Height') return;
    let target, html;
    if (data.domain === 'number') {
      target = isCalibration(data) ? '#calibration-numbers' : '#tuning-numbers';
      html = `<form><label>${escape(data.name)}<span class="field-unit">${escape(data.uom ?? '')}</span><input type="number" required aria-label="${escape(data.name)}"></label><div class="field-actions"><span class="reported">—</span><button type="submit" disabled>Save</button></div></form>`;
    } else if (data.domain === 'button' && (data.name.startsWith('Calibrate') || data.name === 'Restart')) {
      target = data.name === 'Restart' ? '#service-controls' : '#capture-controls';
      html = `<button type="button" disabled>${escape(data.name)}</button>`;
    } else if (data.domain === 'switch' && !data.name.startsWith('Auto-Maintain')) {
      target = isRelay(data) ? '#bench-controls' : data.name === 'Test: inject level fail' ? '#test-controls' : '#service-controls';
      html = `<button type="button" disabled>${escape(data.name)} · —</button>`;
    } else if (['sensor','binary_sensor','text_sensor'].includes(data.domain)) {
      target = '#diagnostic-rows';
      html = `<div class="reading"><span>${escape(data.name)}</span><strong>—</strong></div>`;
    } else return;
    const fragment = document.createElement('template'); fragment.innerHTML = html;
    const row = fragment.content.firstElementChild; row.dataset.entity = data.id; $(target).append(row); rows.set(data.id, row);
    if (data.domain === 'number') {
      const input = row.querySelector('input');
      input.addEventListener('input', () => { input.dataset.dirty = 'true'; });
      row.addEventListener('submit', async event => {
        event.preventDefault();
        if (!row.reportValidity()) return;
        const value = Number(input.value);
        if (await command(states.get(data.id), 'set', {value}, value)) delete input.dataset.dirty;
        scheduleRender();
      });
    } else if (data.domain === 'button') row.addEventListener('click', () => command(states.get(data.id), 'press'));
    else if (data.domain === 'switch') row.addEventListener('click', () => toggle(data.name));
  }
  function accept(data) {
    if (!data.id) return;
    const old = states.get(data.id);
    const domain = data.domain ?? old?.domain ?? (data.name_id?.split('/')[0]) ?? data.id.split('-')[0];
    const merged = {...old, ...data, domain};
    states.set(data.id, merged);
    if (merged.name) names.set(`${domain}/${merged.name}`, data.id);
    addRow(merged); scheduleRender();
  }
  const format = (value, suffix = '', digits = 0) => value === null ? '—' : `${value.toFixed(digits)}${suffix}`;
  function render() {
    const live = online(), bench = testing(), busy = pending.size > 0;
    const activity = text('Lift Activity'), status = text('Lift Status');
    const fault = activity === 'Fault', emergency = activity === 'Emergency Descent';
    const manual = /manual control|^MANUAL /i.test(status);
    const portAngle = /port angle/i.test(status);
    const portDown = /port IMU offline/i.test(status);
    const bypass = flag('switch','Bypass Mode') === true || activity === 'Bypass';
    const pos = text('Lift Position');
    $('#connection').textContent = live ? 'Connected to lift' : 'Disconnected';
    $('#connection').className = `badge ${live ? 'online' : 'offline'}`;
    $('#headline').textContent = !live ? 'Waiting for connection' : bench ? 'Bench test active' : status.replace(' -> ', ' → ');
    $('#position').textContent = live ? pos : 'Unknown';
    $('.overview').classList.toggle('attention', live && (fault || emergency || bench || bypass || portAngle || portDown));
    $('#connection-help').textContent = !live ? 'Live status is unavailable. Web controls are disabled; use the dock controls.' : bench ? 'Automatic control is suspended. Direct outputs are under Advanced → Service & bench.' : emergency ? 'Emergency response is active. Stop requests closed valves.' : fault ? 'Motion is inhibited. Stop acknowledges the fault.' : bypass ? 'Bypass is active. Stop exits Bypass and requests closed valves.' : manual ? 'Position feedback is unavailable. Lift and Lower use manual motion; use Stop to end the move.' : portAngle ? 'Starboard angle is unavailable. Destinations use the port sensor. Leveling stays off until starboard returns.' : portDown ? 'Port angle is unavailable. Destinations stay on starboard. Leveling stays off until port returns.' : 'Choose a destination. The lift controls its movement and leveling.';
    const notices = [];
    if (bench) notices.push('Bench test is active. Stop commands all three outputs off.');
    if (!emergency && !fault && !bypass && flag('binary_sensor','Lift Problem')) notices.push(status);
    if (flag('binary_sensor','Air Loss Alert')) notices.push(`Air loss alert · ${text('Air Loss Detail')}`);
    $('#notice').hidden = !live || !notices.length; $('#notice').textContent = notices.join(' ');
    const commandName = named('select','Lift Command')?.state;
    const commanded = ['Lift','Ready','Guide','Lower'].includes(commandName) ? commandName : null;
    let going = commanded;
    if (bench) going = null;
    else if (!going && emergency) going = /[Ll]owered/.test(status) ? 'Lower' : /Ready/.test(status) ? 'Ready' : null;
    else if (!going && manual) going = /raising/i.test(status) ? 'Lift' : /lowering/i.test(status) ? 'Lower' : null;
    const place = {Lift:'Lifted',Ready:'Ready',Guide:'Guide',Lower:'Lowered'};
    for (const button of document.querySelectorAll('[data-command]')) {
      const name = button.dataset.command;
      button.disabled = !live || !named('button',name) || (name === 'Stop' ? stopBusy : busy || bench || fault || emergency || bypass || status === '—' || (manual && (name === 'Ready' || name === 'Guide')));
      button.classList.toggle('selected', live && !going && place[name] === pos);
      button.classList.toggle('destination', live && going === name);
    }
    const height = live ? number('sensor','Lift Height') : null;
    $('#height').textContent = format(height, '%');
    $('#height-fill').style.width = `${height === null ? 0 : Math.min(100, Math.max(0,height))}%`;
    const bunk = live ? number('sensor','Bunk Height') : null;
    $('#bunk-height').textContent = bunk === null ? 'Bunk height unavailable' : `Bunks ${format(Math.abs(bunk),' in',1)} ${bunk < 0 ? 'below' : 'above'} water`;
    const target = named('number','Target Height');
    const targetInput = $('#height-target');
    const targetValue = finite(target?.value);
    const targetBounds = target ? ['min_value','max_value','step'].map(key => finite(target[key])) : [];
    const targetKnown = targetValue !== null && targetBounds.every(v => v !== null) && targetBounds[2] > 0;
    if (targetKnown) [targetInput.min, targetInput.max, targetInput.step] = targetBounds.map(String);
    if (!targetInput.dataset.dirty && document.activeElement !== targetInput) targetInput.value = live && targetValue !== null ? targetValue : '';
    const heightGoBlocked = !live || busy || !targetKnown || !named('button','Go to Height') || bench || fault || emergency || bypass || manual || status === '—';
    targetInput.disabled = heightGoBlocked;
    $('#height-go').disabled = heightGoBlocked;
    const level = live ? text('Level Status') : 'Unknown';
    const [levelState, ...levelDetails] = level.split(' — ');
    const levelKnown = ['Level OK','Leveling','Auto-level OFF'].includes(levelState);
    $('#level').textContent = levelKnown ? ({'Level OK':'OK','Leveling':'Adjusting','Auto-level OFF':'Auto off'}[levelState]) : 'Unknown';
    $('#level-detail').textContent = levelKnown ? levelDetails.join(' — ') : live && level !== '—' ? level : 'Level comparison unavailable';
    const blower = live ? flag('switch','Blower Relay (Y1) — bench only') : null;
    $('#blower').textContent = blower === null ? 'Unknown' : blower ? 'On' : 'Off';
    for (const [side,openInput,closedInput] of [['Starboard','X02','X01'],['Port','X04','X03']]) {
      const open = live ? flag('binary_sensor',`Valve ${side} FB — Fully Open (${openInput})`) : null;
      const closed = live ? flag('binary_sensor',`Valve ${side} FB — Fully Closed (${closedInput})`) : null;
      const value = open === null || closed === null ? 'Unknown' : open && closed ? 'Fault' : open ? 'Open' : closed ? 'Closed' : 'Moving';
      const output = $(`#${side.toLowerCase()}-valve`);
      output.textContent = value;
      output.classList.toggle('feedback-fault', value === 'Fault');
    }
    $('#boat').textContent = live ? text('Boat Load State') : 'Unknown';
    const temperature = live ? number('sensor','Lift Water Temperature') : null;
    $('#temperature').textContent = format(temperature, ` ${named('sensor','Lift Water Temperature')?.uom ?? '°C'}`, 1);
    $('#last-stop').textContent = live ? text('Last Stop Reason') : '—';
    $('#maintenance-status').textContent = live ? text('Maintain Observe') : 'Waiting for live status';
    $('#calibration-summary').textContent = live ? text('Calibration Summary') : 'Waiting for saved calibration';
    for (const button of document.querySelectorAll('[data-toggle]')) {
      const name = button.dataset.toggle, on = live ? flag('switch', name) : null;
      button.disabled = !live || busy || !named('switch',name) || bench;
      button.textContent = `${name.endsWith('Height') ? 'Height' : 'Level'} · ${on === null ? '—' : on ? 'On' : 'Off'}`;
      button.setAttribute('aria-pressed', String(on === true));
    }
    for (const [id,row] of rows) {
      const data = states.get(id);
      if (!data) {
        row.querySelectorAll('button,input').forEach(el => el.disabled = true);
        if (row.tagName === 'BUTTON') row.disabled = true;
        if (row.querySelector('strong')) row.querySelector('strong').textContent = '—';
        if (row.querySelector('.reported')) row.querySelector('.reported').textContent = 'Reported: —';
        continue;
      }
      if (data.domain === 'number') {
        const input = row.querySelector('input'), value = finite(data.value);
        const bounds = ['min_value','max_value','step'].map(key => finite(data[key]));
        const known = value !== null && bounds.every(v => v !== null) && bounds[2] > 0;
        if (known) { [input.min, input.max, input.step] = bounds.map(String); }
        if (!input.dataset.dirty && document.activeElement !== input) input.value = live && value !== null ? value : '';
        input.disabled = !live || busy || !known;
        row.querySelector('button').disabled = !live || busy || !known;
        row.querySelector('.reported').textContent = `Reported: ${live ? data.state ?? '—' : '—'}`;
      } else if (data.domain === 'switch') {
        const on = live ? flag('switch', data.name) : null;
        row.textContent = `${data.name} · ${on === null ? '—' : on ? 'On' : 'Off'}`;
        row.disabled = !live || busy || on === null || (isRelay(data) && !bench) || (data.name === 'Test: inject level fail' && bench) || (data.name === 'Bypass Mode' && (bench || emergency));
        row.setAttribute('aria-pressed', String(on === true));
      } else if (data.domain === 'button') row.disabled = !live || busy;
      else row.querySelector('strong').textContent = live ? data.state ?? '—' : '—';
    }
    const up = live ? number('sensor','Uptime') : null;
    $('#uptime').textContent = up === null ? 'Uptime —' : `Uptime ${up < 3600 ? `${Math.floor(up / 60)} min` : `${Math.floor(up / 3600)}h ${Math.floor(up % 3600 / 60)}m`}`;
  }

  async function command(data, action, values = {}, expected) {
    const isStop = data?.domain === 'button' && data.name === 'Stop';
    if (!online() || !data || (!isStop && pending.size) || (isStop && stopBusy)) return false;
    if (isRelay(data) && !testing()) return false;
    const ticket = ++generation;
    if (isStop) { for (const controller of pending) controller.abort(); stopBusy = true; }
    const controller = new AbortController(); pending.add(controller); render();
    const timeout = setTimeout(() => controller.abort(), 4000);
    // Entity names come from the device's metadata, not lossy legacy ID slugs.
    const path = `/${data.domain}/${encodeURIComponent(data.name)}`;
    const query = new URLSearchParams(values);
    try {
      const response = await fetch(`${path}/${action}${query.size ? `?${query}` : ''}`, {method:'POST',signal:controller.signal});
      if (!response.ok) throw new Error(`Controller returned ${response.status}`);
      if (ticket === generation) message(`${data.name} request sent. Watch the lift status for the result.`);
      if (data.domain !== 'button') {
        const read = await fetch(`${path}?detail=all`, {signal:controller.signal});
        if (!read.ok) throw new Error('Request sent; could not read back the reported setting.');
        const result = await read.json(); accept(result);
        const actual = typeof expected === 'boolean' ? flag(data.domain,data.name) : finite(result.value);
        const matches = typeof expected === 'boolean' ? actual === expected : actual !== null && Math.abs(actual - expected) < Math.max(1,Math.abs(expected)) * 1e-6;
        if (ticket === generation) message(matches ? `${data.name} updated.` : `${data.name} request sent; the controller has not reported that value yet.`);
      }
      return true;
    } catch (error) {
      if (ticket === generation) message(error.name === 'AbortError' ? 'No response confirmed. The request may have reached the lift; check its status or use the dock controls.' : error.message, true);
      return false;
    } finally { clearTimeout(timeout); pending.delete(controller); if (isStop) stopBusy = false; render(); }
  }
  function toggle(name) {
    const value = flag('switch',name); if (value === null) return;
    return command(named('switch',name), value ? 'turn_off' : 'turn_on', {}, !value);
  }
  document.querySelectorAll('[data-command]').forEach(button => button.addEventListener('click', () => command(named('button',button.dataset.command), 'press')));
  $('#height-target').addEventListener('input', () => { $('#height-target').dataset.dirty = 'true'; });
  $('#height-goto').addEventListener('submit', async event => {
    event.preventDefault();
    const input = $('#height-target');
    if (!input.reportValidity()) return;
    const value = Number(input.value);
    const data = named('number','Target Height');
    const button = named('button','Go to Height');
    if (!data || !button) return;
    const current = finite(data.value);
    if (current === null || Math.abs(current - value) >= 0.05) {
      if (!await command(data, 'set', {value}, value)) return;
    }
    delete input.dataset.dirty;
    await command(button, 'press');
  });
  document.querySelectorAll('[data-toggle]').forEach(button => button.addEventListener('click', () => toggle(button.dataset.toggle)));
  function openEvents() {
    const previous = events;
    previous?.close();
    const source = new EventSource('/events');
    events = source;
    source.onopen = () => {
      if (events !== source) return;
      states.clear(); names.clear(); connected = true; lastEvent = Date.now(); scheduleRender();
    };
    source.onerror = () => { if (events !== source) return; connected = false; scheduleRender(); };
    source.addEventListener('state', event => {
      if (events !== source) return;
      lastEvent = Date.now();
      try { accept(JSON.parse(event.data)); } catch { message('An update could not be read. Waiting for fresh status.', true); }
    });
    source.addEventListener('ping', () => { if (events !== source) return; lastEvent = Date.now(); scheduleRender(); });
  }
  openEvents();
  document.addEventListener('visibilitychange', () => {
    if (document.visibilityState !== 'visible') return;
    if (!online()) openEvents();
    else scheduleRender();
  });
  setInterval(scheduleRender, 1000);
  render();
})();
