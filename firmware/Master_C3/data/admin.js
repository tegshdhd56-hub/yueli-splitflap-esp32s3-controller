// All physical operations are authorized and checked again by the ESP32.
(() => {
  const el = id => document.getElementById(id);
  const panel = el('adminPanel'), loginForm = el('adminLoginForm');
  const controls = el('adminControls'), fields = el('calibrationFields');
  const notice = el('adminNotice'), deviceNotice = el('deviceStateNotice');
  const submit = el('messageSubmit'), homeConfirm = el('confirmHomedButton');
  const stoppedConfirm = el('calibrationConfirmStopped');
  let token = '', epoch = 0, polling = 0, pollVersion = 0, sending = false, verified = false;
  let device = null, lastReported = 0, unitRun = 0;
  let unitSnapshot = null, unitFresh = false, unitRequest = null, addressTimer = 0, staleQueryId = null;
  const pageLabels = ['空白', ...'ABCDEFGHIJKLMNOPQRSTUVWXYZ', ...'0123456789', ':', '/', '?', '!', '.', '-', '$', '@', '#', '&', ';', '(', ')', '+', '=', "'", '"', ',', '°', '%', '白色', '红色', '橙色', '黄色', '绿色', '蓝色', '紫色'];
  pageLabels.forEach((label, page) => {
    const option = document.createElement('option');
    option.value = String(page); option.textContent = `${page} · ${label}`;
    el('calibrationPage').appendChild(option);
  });
  function inform(text, error = false) {
    notice.textContent = text; notice.classList.toggle('error', error);
  }
  function clearSession() {
    token = ''; ++epoch; ++unitRun; lastReported = 0;
    clearTimeout(addressTimer); unitRequest = null;
    el('developerIntro').textContent = 'For developers only. Sign in to access unit calibration.';
    controls.classList.toggle('hidden', true); loginForm.classList.toggle('hidden', false);
    stoppedConfirm.classList.toggle('hidden', true); fields.disabled = true;
    resetUnitValues();
  }
  const errors = {
    incorrect_password: 'Incorrect developer password.', login_rate_limited: 'Too many failed attempts. Try again in 30 seconds.',
    authentication_required: 'Developer session expired. Please sign in again.',
    invalid_calibration_parameters: '校准参数不正确，请检查地址、页码或步数。',
    startup_homing: '上电归零指令仍在发送或等待，请先确认矩阵已停稳。',
    motion_wait: '请等待单元停止运动，然后点击确认停稳。',
    calibration_busy: '上一条指令仍在等待，请稍后再操作。',
    calibration_queue_full: '指令队列已满，请稍后重试。',
    readback_busy: '另一条偏移查询仍在等待，请稍后再读取。',
    invalid_unit_parameters: '请输入有效的单元地址和页码。'
  };
  async function request(url, options = {}, authenticated = false) {
    const controller = new AbortController();
    const timeout = setTimeout(() => controller.abort(), 6000);
    try {
      const response = await fetch(url, {cache: 'no-store', ...options, signal: controller.signal,
        headers: {...(options.headers || {}), ...(authenticated ? {Authorization: `Bearer ${token}`} : {})}});
      const data = await response.json();
      if (!response.ok) {
        const error = new Error(errors[data.error] || data.message || (token ? `请求失败 (${response.status})` : `Request failed (${response.status})`));
        error.status = response.status; throw error;
      }
      return data;
    } finally { clearTimeout(timeout); }
  }
  function resetUnitValues() {
    unitSnapshot = null; unitFresh = false; staleQueryId = null;
    el('calibrationCurrentAngle').textContent = '—';
    el('calibrationPageAngle').textContent = '—';
    el('calibrationReportedOffset').textContent = '—';
    el('calibrationOffsetSource').textContent = '尚无有效 OFGET 应答。';
    el('calibrationUseOffset').disabled = true;
  }
  function unitSelection() {
    return {dip: integer('calibrationDip', 0, 255), page: integer('calibrationPage', 0, 63)};
  }
  function stillSelected(selection) {
    try { const current = unitSelection(); return current.dip === selection.dip && current.page === selection.page; }
    catch (_) { return false; }
  }
  function queryPending(query = unitSnapshot?.query) { return !!query && (query.state === 'queued' || query.state === 'waiting'); }
  function canReadOffset() {
    return token && verified && !sending && device?.boot?.state === 'ready' && !device.busy && !device.awaitingMotionConfirmation && !queryPending();
  }
  function validUnsigned(value) { return Number.isInteger(value) && value >= 0 && value <= 4294967295; }
  function validOffset(offset) {
    return offset && offset.source === 'ofget' && Number.isInteger(offset.value) && offset.value >= -360000 && offset.value <= 360000 &&
      validUnsigned(offset.receivedAt) && validUnsigned(offset.queryId) && offset.queryId > 0 && validUnsigned(offset.commandId);
  }
  function validateUnitResponse(data, selection) {
    if (data.dip !== selection.dip || data.page !== selection.page) throw new Error('返回的单元地址或页码不匹配，已忽略。');
    if (data.reportedOffset !== null && data.reportedOffset !== undefined && !validOffset(data.reportedOffset)) throw new Error('偏移应答来源或数值无效，已忽略。');
    const query = data.query;
    if (query && (query.dip !== selection.dip || !validUnsigned(query.id) || query.id === 0 || !validUnsigned(query.commandId) ||
      !Number.isInteger(query.page) || query.page < -1 || query.page > 63 || !['queued','waiting','received','timeout','cancelled'].includes(query.state) ||
      (query.expectedOffset !== null && (!Number.isInteger(query.expectedOffset) || query.expectedOffset < -360000 || query.expectedOffset > 360000)))) {
      throw new Error('偏移查询记录无效，已忽略。');
    }
    return data;
  }
  function renderUnitValues() {
    el('calibrationCurrentAngle').textContent = '固件不支持回读';
    el('calibrationPageAngle').textContent = '固件不支持回读';
    const offset = unitSnapshot?.reportedOffset;
    el('calibrationReportedOffset').textContent = validOffset(offset) ? `${unitFresh ? '' : '上一次有效回读 · '}${offset.value} 毫度 = ${(offset.value / 1000).toFixed(3)}°` : '—';
    el('calibrationOffsetSource').textContent = validOffset(offset) ? `来源：OFGET · DIP ${unitSnapshot.dip} · ESP32 运行 ${(offset.receivedAt / 1000).toFixed(3)} 秒时收到 · 查询 #${offset.queryId}${unitFresh ? '' : ' · 当前查询尚未验证此值'}` : '尚无有效 OFGET 应答。';
    el('calibrationUseOffset').disabled = !unitFresh || !canReadOffset();
  }
  function acceptUnitResponse(data, selection) {
    validateUnitResponse(data, selection);
    unitSnapshot = data;
    const query = data.query, offset = data.reportedOffset;
    unitFresh = query?.state === 'received' && validOffset(offset) && offset.queryId === query.id && offset.commandId === query.commandId && query.id !== staleQueryId;
    renderUnitValues();
    let message = '尚未查询本单元的全局偏移，请点击“读取当前偏移”。';
    if (query?.state === 'queued') message = '偏移查询已排队。若单元正在运动，请先观察并确认停稳；写入指令后需等待冷却。';
    else if (query?.state === 'waiting') message = 'CMD-OFGET 已发送，等待本单元应答（最长 2 秒）。';
    else if (query?.state === 'timeout') message = '偏移查询超时：2 秒内未收到本地址的有效 OFGET 应答。上一次值仅供参考，请检查连接、单元固件及地址，再手动重试。';
    else if (query?.state === 'cancelled') message = '本次偏移查询已取消；上一次值不代表当前状态。';
    else if (query?.state === 'received') {
      if (!unitFresh) message = '应答与本次查询不匹配；上一次值不代表当前状态，请重新读取。';
      else if (query.expectedOffset !== null) {
        const matches = offset.value === query.expectedOffset && query.matchesExpected === true;
        message = matches ? `回读与本次写入值 ${query.expectedOffset} 毫度一致；仅确认运行时偏移，不代表 Flash 掉电保存成功。` : `回读值 ${offset.value} 毫度与本次写入值 ${query.expectedOffset} 毫度不一致，或尚未通过核对；请检查后重试。`;
      } else message = '已收到本地址的真实 OFGET 全局偏移。此值不是本页角度，也不是 LEARN 保存完成回执。';
    }
    el('calibrationValueStatus').textContent = message;
    el('calibrationValueStatus').classList.toggle('error', query?.state === 'timeout' || (query?.state === 'received' && (!unitFresh || (query.expectedOffset !== null && (query.matchesExpected !== true || offset?.value !== query.expectedOffset)))));
    if (device) applyDevice(device);
  }
  function unitFailure(error) {
    staleQueryId = unitSnapshot?.reportedOffset?.queryId ?? staleQueryId;
    unitFresh = false; renderUnitValues();
    el('calibrationValueStatus').textContent = `${error.message || '未能读取单元记录，请检查连接。'}${unitSnapshot?.reportedOffset ? ' 已显示的值为上一次回读，不能作为本次查询结果。' : ''}`;
    el('calibrationValueStatus').classList.toggle('error', true);
    if (error.status === 401) { clearSession(); inform(errors.authentication_required, true); unavailable(); }
  }
  async function refreshUnitValues() {
    if (!token) return;
    let selection;
    try { selection = unitSelection(); } catch (error) { unitFailure(error); return; }
    if (unitRequest && unitRequest.session === epoch && unitRequest.dip === selection.dip && unitRequest.page === selection.page) return;
    const run = ++unitRun, session = epoch;
    unitRequest = {run, session, ...selection};
    el('calibrationValueAddress').textContent = `单元地址 ${selection.dip} · 页码 ${selection.page}`;
    try {
      const data = await request(`/admin/unit?dip=${selection.dip}&page=${selection.page}`, {}, true);
      if (run !== unitRun || session !== epoch || !token || !stillSelected(selection)) return;
      acceptUnitResponse(data, selection);
    } catch (error) {
      if (run === unitRun && session === epoch && stillSelected(selection)) unitFailure(error);
    } finally { if (unitRequest?.run === run) unitRequest = null; }
  }
  async function readOffset() {
    if (!canReadOffset()) return;
    let selection;
    try { selection = unitSelection(); } catch (error) { unitFailure(error); return; }
    const session = ++epoch, run = ++unitRun;
    unitRequest = null; sending = true; clearTimeout(polling); clearTimeout(addressTimer);
    staleQueryId = unitSnapshot?.reportedOffset?.queryId ?? staleQueryId;
    unitFresh = false; renderUnitValues(); applyDevice(device);
    el('calibrationValueAddress').textContent = `单元地址 ${selection.dip} · 页码 ${selection.page}`;
    el('calibrationValueStatus').textContent = '正在请求 CMD-OFGET 全局偏移回读…';
    try {
      const data = await request('/admin/unit/read', {method: 'POST', body: new URLSearchParams(selection)}, true);
      if (run !== unitRun || session !== epoch || !token || !stillSelected(selection)) return;
      acceptUnitResponse(data, selection);
    } catch (error) {
      if (run === unitRun && session === epoch && stillSelected(selection)) unitFailure(error);
    } finally { sending = false; poll(); }
  }
  function selectionChanged(isAddress) {
    ++unitRun; unitRequest = null; clearTimeout(addressTimer); resetUnitValues();
    el('calibrationValueAddress').textContent = '单元地址 — · 页码 —';
    el('calibrationValueStatus').textContent = '地址或页码已改变，等待读取所选单元。';
    addressTimer = setTimeout(() => {
      if (isAddress && canReadOffset()) readOffset(); else refreshUnitValues();
    }, isAddress ? 350 : 0);
  }
  el('calibrationUseOffset').addEventListener('click', () => {
    if (!unitFresh || !canReadOffset() || !unitSnapshot || !stillSelected(unitSnapshot) || !validOffset(unitSnapshot.reportedOffset)) return;
    el('calibrationOffset').value = String(unitSnapshot.reportedOffset.value);
    inform('已将本次真实回读的全局偏移填入输入框；尚未发送写入指令。');
    el('calibrationOffset').focus();
  });
  function applyDevice(data) {
    device = data; verified = true;
    const boot = data.boot || {}, ready = boot.state === 'ready';
    const waitSeconds = Math.ceil((data.retryAfterMs || 0) / 1000);
    let message = '';
    if (boot.state === 'waiting') {
      // The ESP32 owns this timer. Polling displays its remaining time without
      // restarting it on refresh or advancing any state in the browser.
      const duration = boot.selfCheckDurationMs, remaining = boot.selfCheckRemainingMs;
      if (validUnsigned(duration) && duration > 0 && validUnsigned(remaining) && remaining <= duration) {
        const seconds = Math.ceil(remaining / 1000);
        message = seconds > 0
          ? `Unit self-check: ${seconds}s remaining. No unit commands are sent during this period. Startup homing follows automatically.`
          : 'Unit self-check: 0s remaining. Waiting for the device to start homing; display controls remain locked.';
      } else message = 'Unit self-check is in progress. Waiting for device timing information; display controls remain locked.';
    }
    else if (boot.state === 'homing') message = `Startup homing: ${boot.completed} / ${boot.total} blank-page commands sent.`;
    else if (boot.state === 'settling') message = `All blank-page commands have been sent. Once every unit has stopped, confirm below to continue, or wait for the host timer${boot.autoResumeInMs ? ` (about ${Math.ceil(boot.autoResumeInMs / 60000)} minutes remaining)` : ''}. The units do not report completion.`;
    else if (data.awaitingMotionConfirmation) message = 'Display paused. A developer must confirm that the calibration movement has stopped.';
    else if (data.calibrationActive) message = 'Developer calibration is active. Display updates and automatic playback are paused.';
    else if (data.busy) message = `Waiting for the previous command. Try again in about ${waitSeconds} seconds.`;
    deviceNotice.textContent = message; deviceNotice.classList.toggle('hidden', !message);
    submit.disabled = !ready || !!data.calibrationActive || !!data.busy || !!data.awaitingMotionConfirmation;
    homeConfirm.classList.toggle('hidden', boot.state !== 'settling');
    homeConfirm.disabled = sending || boot.state !== 'settling' || (boot.retryAfterMs || 0) > 0;
    fields.disabled = !token || sending || !ready || !!data.busy || !!data.awaitingMotionConfirmation || queryPending();
    el('calibrationRefreshValues').disabled = !canReadOffset();
    el('calibrationUseOffset').disabled = !unitFresh || !canReadOffset();
    stoppedConfirm.classList.toggle('hidden', !token || !ready || !data.awaitingMotionConfirmation);
    stoppedConfirm.disabled = !token || !ready || sending || waitSeconds > 0;
    stoppedConfirm.textContent = waitSeconds > 0 ? `请观察单元运动 · 至少再等 ${waitSeconds} 秒` : '我已确认单元停稳，继续校准';
  }
  function applyAdmin(data) {
    applyDevice(data);
    const command = data.lastCommand;
    if (command) {
      el('adminLastCommand').textContent = `${command.command} · ${command.state === 'sent' ? '已发送（无完成回执）' : '等待发送'}`;
      if (command.state === 'sent' && command.id !== lastReported) {
        lastReported = command.id;
        inform('指令已发送，请观察实际位置验证。定位或微调后请确认单元停稳，再进行下一步。');
      }
    }
    const lines = Array.isArray(data.rx) ? data.rx.slice(-12) : [];
    el('adminRxLog').textContent = lines.length ? lines.map(line => `[${(Number(line.at) / 1000).toFixed(1)}s] ${line.text}`).join('\n') : '暂无接收数据（此单元固件不发送校准完成回执）';
    if (lines.some(line => /^SFDJAK[0-9A-F]{24}$/.test(line.text))) inform('总线收到未授权单元提示。请用原厂工具检查授权；该提示无法确定对应 DIP。', true);
    refreshUnitValues();
  }
  function unavailable() {
    verified = false; fields.disabled = true; submit.disabled = true;
    homeConfirm.disabled = true; stoppedConfirm.disabled = true;
    el('calibrationRefreshValues').disabled = true;
    staleQueryId = unitSnapshot?.reportedOffset?.queryId ?? staleQueryId;
    unitFresh = false; renderUnitValues();
    deviceNotice.textContent = 'Device status is unavailable. Check the connection; this page will refresh automatically.';
    deviceNotice.classList.toggle('hidden', false);
  }
  async function poll() {
    clearTimeout(polling);
    if (sending) return;
    const current = epoch, version = ++pollVersion, authenticated = !!token;
    try {
      const data = await request(authenticated ? '/admin/status' : '/device/state', {}, authenticated);
      if (current !== epoch || version !== pollVersion) return;
      authenticated ? applyAdmin(data) : applyDevice(data);
    } catch (error) {
      if (current !== epoch || version !== pollVersion) return;
      if (authenticated && error.status === 401) { clearSession(); inform(errors.authentication_required, true); }
      unavailable();
    } finally {
      if (version === pollVersion && !sending) {
        if (current === epoch) polling = setTimeout(poll, token || device?.boot?.state !== 'ready' || device?.busy ? 1000 : 5000);
        else polling = setTimeout(poll, 100);
      }
    }
  }
  loginForm.addEventListener('submit', async event => {
    event.preventDefault(); if (sending) return;
    sending = true; el('adminLoginButton').disabled = true; ++epoch;
    clearTimeout(polling);
    try {
      const data = await request('/admin/login', {method: 'POST', body: new URLSearchParams({password: el('adminPassword').value})});
      if (!data.token) throw new Error('The device did not return a valid developer session.');
      token = data.token; el('adminPassword').value = '';
      el('developerIntro').textContent = '开发者校准期间暂停普通显示和自动播放。校准完成后请退出开发者模式。';
      controls.classList.toggle('hidden', false); loginForm.classList.toggle('hidden', true);
      applyDevice(data); inform('管理员已解锁。先选择实际 DIP 地址，再定位、微调、保存；操作后请观察验证。');
    } catch (error) { inform(error.message, true); }
    finally {
      sending = false; el('adminLoginButton').disabled = false;
      if (token && canReadOffset()) readOffset(); else { if (token) refreshUnitValues(); poll(); }
    }
  });
  el('adminLogoutButton').addEventListener('click', async () => {
    if (sending) return; sending = true; ++epoch; clearTimeout(polling); fields.disabled = true;
    try { await request('/admin/logout', {method: 'POST'}, true); clearSession(); inform('Developer mode locked. If a movement is still unconfirmed, sign in again to confirm it has stopped.'); }
    catch (error) { if (error.status === 401) clearSession(); inform(error.message, true); }
    finally { sending = false; poll(); }
  });
  function integer(id, min, max) {
    const value = el(id).value;
    if (!/^-?\d+$/.test(value) || Number(value) < min || Number(value) > max) throw new Error(`请输入 ${min} 至 ${max} 之间的整数。`);
    return Number(value);
  }
  async function sendAction(action, extra = {}) {
    if (sending || !token || !verified) return;
    if (device?.boot?.state !== 'ready') return;
    if (action !== 'confirm_stopped' && (device.busy || device.awaitingMotionConfirmation || queryPending())) return;
    try {
      const params = {action};
      if (action !== 'confirm_stopped') {
        params.dip = integer('calibrationDip', 0, 255);
        params.page = extra.page ?? integer('calibrationPage', 0, 63);
      }
      if (action === 'fine') params.steps = extra.sign * integer('calibrationSteps', 1, 200);
      if (action === 'offset') params.offset = integer('calibrationOffset', -360000, 360000);
      if (action === 'clear' && !window.confirm(`清除 DIP ${params.dip} 的整张校准表并恢复默认角度？原校准数据会被覆盖。`)) return;
      if (action === 'goto' && extra.page === 0) { el('calibrationPage').value = '0'; selectionChanged(false); }
      sending = true; ++epoch; ++unitRun; unitRequest = null; clearTimeout(polling); clearTimeout(addressTimer);
      staleQueryId = unitSnapshot?.reportedOffset?.queryId ?? staleQueryId;
      unitFresh = false; renderUnitValues(); fields.disabled = true; stoppedConfirm.disabled = true;
      const data = await request('/admin/calibrate', {method: 'POST', body: new URLSearchParams(params)}, true);
      if (action === 'confirm_stopped') inform('已记录停稳确认；等待偏移回读完成后即可继续校准。');
      else { el('adminLastCommand').textContent = `${data.command || action} · 等待发送`; inform('指令已排队，正在等待 ESP32 发送。'); }
      refreshUnitValues();
    } catch (error) { if (error.status === 401) clearSession(); inform(error.message, true); }
    finally { sending = false; poll(); }
  }
  el('calibrationGoto').addEventListener('click', () => sendAction('goto'));
  el('calibrationBlank').addEventListener('click', () => sendAction('goto', {page: 0}));
  el('calibrationMinus').addEventListener('click', () => sendAction('fine', {sign: -1}));
  el('calibrationPlus').addEventListener('click', () => sendAction('fine', {sign: 1}));
  el('calibrationLearn').addEventListener('click', () => sendAction('learn'));
  el('calibrationSetOffset').addEventListener('click', () => sendAction('offset'));
  el('calibrationClear').addEventListener('click', () => sendAction('clear'));
  el('calibrationRefreshValues').addEventListener('click', readOffset);
  el('calibrationDip').addEventListener('input', () => selectionChanged(true));
  el('calibrationPage').addEventListener('change', () => selectionChanged(false));
  stoppedConfirm.addEventListener('click', () => sendAction('confirm_stopped'));
  homeConfirm.addEventListener('click', async () => {
    if (sending || !verified || device?.boot?.state !== 'settling' || (device.boot.retryAfterMs || 0) > 0) return;
    sending = true; homeConfirm.disabled = true; ++epoch; clearTimeout(polling);
    try { await request('/device/confirm-homed', {method: 'POST'}); }
    catch (error) { inform(error.message, true); }
    finally { sending = false; poll(); }
  });
  panel.addEventListener('toggle', () => { if (panel.open && !sending) poll(); });
  // Stay locked until the first response establishes the device's boot state.
  submit.disabled = true; fields.disabled = true;
  homeConfirm.disabled = true; stoppedConfirm.disabled = true;
  el('calibrationRefreshValues').disabled = true;
  el('calibrationUseOffset').disabled = true;
  poll();
})();
