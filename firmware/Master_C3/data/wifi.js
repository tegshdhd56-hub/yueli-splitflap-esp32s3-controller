// Wi-Fi provisioning is separate from the display message form.
(() => {
  const panel = document.getElementById('networkPanel');
  const form = document.getElementById('wifiForm');
  const network = document.getElementById('wifiNetwork');
  const manualField = document.getElementById('wifiManualField');
  const manualSsid = document.getElementById('inputWifiSsid');
  const password = document.getElementById('inputWifiPass');
  const showPassword = document.getElementById('wifiShowPassword');
  const scanButton = document.getElementById('wifiScanButton');
  const connectButton = document.getElementById('wifiConnectButton');
  const scanStatus = document.getElementById('wifiScanStatus');
  const connectionStatus = document.getElementById('wifiConnectionStatus');
  const currentIp = document.getElementById('wifiCurrentIp');
  const currentSsid = document.getElementById('wifiCurrentSsid');
  let networks = [];
  let activeSsid = '';
  let connecting = false;
  let scanning = false;
  let scanRun = 0;
  let connectionRun = 0;
  let liveStatusTimer = 0;
  let liveStatusRun = 0;
  const wait = ms => new Promise(resolve => setTimeout(resolve, ms));

  async function request(url, options = {}, timeoutMs = 6500) {
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), timeoutMs);
    try {
      const response = await fetch(url, { cache: 'no-store', ...options, signal: controller.signal });
      const data = await response.json();
      if (!response.ok) {
        const error = new Error(data.message || 'The device could not accept this request.');
        error.status = response.status;
        throw error;
      }
      return data;
    } finally { clearTimeout(timer); }
  }

  function selectedNetwork() {
    return network.value === 'manual' ? null : networks[Number(network.value)];
  }

  function updateSelection() {
    const manual = network.value === 'manual';
    const selected = selectedNetwork();
    manualField.classList.toggle('hidden', !manual);
    manualSsid.required = manual;
    const open = !manual && selected && !selected.secure;
    password.disabled = connecting || !!open;
    password.placeholder = open ? 'No password required' : 'Wi-Fi password';
    showPassword.disabled = password.disabled;
    connectButton.disabled = connecting || scanning || (!manual && !selected);
  }

  function setBusy(busy) {
    connecting = busy;
    network.disabled = busy;
    manualSsid.disabled = busy;
    scanButton.disabled = busy || scanning;
    connectButton.textContent = busy ? 'Connecting…' : 'Connect';
    updateSelection();
  }

  function message(text, state = '') {
    connectionStatus.textContent = text;
    connectionStatus.dataset.state = state;
  }

  function renderCurrentAddress(data) {
    const address = data.currentIp || data.ip || '';
    const connected = data.connected === true && address && address !== '0.0.0.0';
    currentIp.textContent = connected ? address : '—';
    currentSsid.textContent = connected ? (data.currentSsid || data.ssid || 'Connected') : 'Not connected';
  }

  async function refreshCurrentAddress() {
    clearTimeout(liveStatusTimer);
    const run = ++liveStatusRun;
    const attempt = connectionRun;
    if (!panel.open) return;
    try {
      // The connection monitor already refreshes the live address while active.
      if (!connecting) {
        const data = await request('/wifi/status');
        if (run !== liveStatusRun || !panel.open || attempt !== connectionRun || connecting) return;
        renderCurrentAddress(data);
      }
    } catch (_) {
      if (run === liveStatusRun && attempt === connectionRun && !connecting) {
        currentIp.textContent = '—';
        currentSsid.textContent = 'Device unreachable · IP not confirmed';
      }
    } finally {
      if (run === liveStatusRun && panel.open) liveStatusTimer = setTimeout(refreshCurrentAddress, 5000);
    }
  }

  function renderNetworks(items) {
    const previous = selectedNetwork()?.ssid || activeSsid;
    const wasManual = network.value === 'manual';
    networks = items.filter(item => item && typeof item.ssid === 'string' && item.ssid.length)
      .sort((a, b) => b.rssi - a.rssi);
    network.replaceChildren();
    networks.forEach((item, index) => {
      const option = document.createElement('option');
      option.value = String(index);
      const strength = item.rssi >= -60 ? 'Strong' : item.rssi >= -75 ? 'Good' : 'Weak';
      option.textContent = `${item.ssid} · ${strength}${item.secure ? '' : ' · Open'}`;
      network.appendChild(option);
    });
    const manual = document.createElement('option');
    manual.value = 'manual';
    manual.textContent = 'Other / hidden network…';
    network.appendChild(manual);
    const priorIndex = networks.findIndex(item => item.ssid === previous);
    network.value = wasManual ? 'manual' : priorIndex >= 0 ? String(priorIndex) : networks.length ? '0' : 'manual';
    updateSelection();
  }

  async function scan(refresh = false) {
    if (connecting || scanning) return;
    const run = ++scanRun;
    scanning = true;
    scanButton.disabled = true;
    updateSelection();
    scanStatus.textContent = 'Scanning nearby Wi-Fi…';
    // A saved-network connection can take 30 seconds before the scan starts.
    // Short radio interruptions while changing channels are retried separately.
    const deadline = Date.now() + 65000;
    let url = `/wifi/scan${refresh ? '?refresh=1' : ''}`;
    let receivedStatus = false;
    try {
      while (run === scanRun && !connecting && Date.now() < deadline) {
        let data;
        try {
          data = await request(url, {}, Math.min(10000, deadline - Date.now()));
          receivedStatus = true;
        } catch (error) {
          if (run !== scanRun || connecting) return;
          if (error.status) throw error;
          // A lost refresh response may still have started a scan. Poll it;
          // do not repeatedly restart the device's radio scan.
          scanStatus.textContent = 'Waiting for YUELI… Keep this page open.';
        }
        url = '/wifi/scan';
        if (run !== scanRun || connecting) return;
        if (data?.state === 'ready') {
          renderNetworks(Array.isArray(data.networks) ? data.networks : []);
          scanStatus.textContent = networks.length ? `${networks.length} nearby network(s) · 2.4 GHz` : 'No networks found. Move closer or enter a hidden network name.';
          return;
        }
        if (data && data.state !== 'scanning' && data.state !== 'busy') {
          throw new Error(data.message || 'The device could not scan Wi-Fi. Select Refresh to try again.');
        }
        if (data) scanStatus.textContent = data.state === 'busy' ? 'Wi-Fi is connecting. Waiting to scan…' : 'Scanning nearby Wi-Fi…';
        if (Date.now() >= deadline) break;
        await wait(1000);
      }
      if (run !== scanRun || connecting) return;
      throw new Error(receivedStatus ? 'Scan timed out. Select Refresh to try again.' : 'YUELI is unreachable. Check your Wi-Fi connection, then select Refresh.');
    } catch (error) {
      if (run !== scanRun || connecting) return;
      if (!networks.length) renderNetworks([]);
      scanStatus.textContent = error.message || 'The device could not scan Wi-Fi. Select Refresh to try again.';
    } finally {
      if (run === scanRun) {
        scanning = false;
        scanButton.disabled = connecting;
        updateSelection();
      }
    }
  }

  function renderConnection(data) {
    renderCurrentAddress(data);
    activeSsid = data.currentSsid || (data.state === 'connected' ? data.ssid : '') || activeSsid;
    const restored = data.connected && data.currentSsid && data.currentSsid !== data.ssid ? ` Still connected to ${data.currentSsid}.` : '';
    switch (data.state) {
      case 'connecting':
        message(`Connecting to ${data.ssid || 'Wi-Fi'}… Keep this page open.`);
        return false;
      case 'connected':
        password.value = '';
        password.type = 'password';
        showPassword.checked = false;
        message(`Connected to ${data.currentSsid || data.ssid}.${data.message ? ` ${data.message}` : ''}`, 'connected');
        break;
      case 'wrong_password':
        message(`Incorrect password or authentication rejected. Check the password and try again.${restored}`, 'error');
        break;
      case 'not_found':
        message(`Network not found. Move closer to the router and refresh.${restored}`, 'error');
        break;
      case 'failed':
        message(`Connection failed. Check the signal, password and router settings. ${data.message || ''}${restored}`, 'error');
        break;
      default:
        message(data.connected ? `Connected to ${data.currentSsid}.` : 'Choose a network to connect. Only successful connections are saved.', data.connected ? 'connected' : '');
    }
    return true;
  }

  async function monitorConnection(run, expectedRequestId = '') {
    const deadline = Date.now() + 70000;
    while (run === connectionRun && Date.now() < deadline) {
      try {
        const data = await request('/wifi/status');
        if (run !== connectionRun) return;
        if (expectedRequestId && data.clientRequestId !== expectedRequestId) {
          message('Waiting for the device to confirm this connection request…');
        } else if (renderConnection(data)) { setBusy(false); return; }
      } catch (_) {
        if (run !== connectionRun) return;
        currentIp.textContent = '—';
        currentSsid.textContent = 'Waiting for device';
        message('Waiting for YUELI… If Wi-Fi disconnected, reconnect to YUELI and keep this page open.');
      }
      await wait(1400);
    }
    if (run === connectionRun) {
      setBusy(false);
      message('Could not confirm the connection. Reconnect to YUELI and refresh this page to check its status.', 'error');
    }
  }

  form.addEventListener('submit', async event => {
    event.preventDefault();
    if (connecting || scanning) return;
    const selected = selectedNetwork();
    const ssid = network.value === 'manual' ? manualSsid.value : selected?.ssid || '';
    const pass = selected && !selected.secure && network.value !== 'manual' ? '' : password.value;
    if (!ssid || new TextEncoder().encode(ssid).length > 32) {
      message('Choose a network or enter its name (up to 32 UTF-8 bytes).', 'error');
      return;
    }
    if (pass.length && (pass.length < 8 || pass.length > 64 || (pass.length === 64 && !/^[0-9a-f]+$/i.test(pass)))) {
      message('Password must contain 8–63 characters (or a 64-digit hex key).', 'error');
      password.focus();
      return;
    }
    if (selected?.secure && !pass) {
      message('Enter the network password.', 'error');
      password.focus();
      return;
    }
    ++scanRun;
    const run = ++connectionRun;
    const clientRequestId = `${Date.now().toString(36)}-${Math.random().toString(36).slice(2, 14)}`;
    setBusy(true);
    message(`Connecting to ${ssid}… Keep this page open.`);
    try {
      await request('/wifi/connect', {
        method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: new URLSearchParams({ ssid, password: pass, clientRequestId }).toString()
      });
      await monitorConnection(run, clientRequestId);
    } catch (error) {
      if (run !== connectionRun) return;
      if (!error.status) {
        // A lost response can follow a successful request when the AP changes channel.
        message('Waiting for YUELI to confirm the connection…');
        await monitorConnection(run, clientRequestId);
      } else if (error.status === 409) {
        setBusy(false);
        message('The device is already connecting to a network. Wait for it to finish, then try again.', 'error');
      } else {
        setBusy(false);
        message(error.message, 'error');
      }
    }
  });
  network.addEventListener('change', updateSelection);
  showPassword.addEventListener('change', () => { password.type = showPassword.checked ? 'text' : 'password'; });
  scanButton.addEventListener('click', () => scan(true));
  panel.addEventListener('toggle', () => {
    if (panel.open) { scan(true); refreshCurrentAddress(); }
    else { ++liveStatusRun; clearTimeout(liveStatusTimer); }
  });
  updateSelection();
  request('/wifi/status').then(data => {
    if (connectionRun) return;
    if (!renderConnection(data)) {
      setBusy(true);
      monitorConnection(++connectionRun);
    }
  }).catch(() => { if (!connectionRun) message('Connect to the YUELI hotspot to manage Wi-Fi.'); });
})();
