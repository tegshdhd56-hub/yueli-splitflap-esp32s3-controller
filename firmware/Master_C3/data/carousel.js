/* Persisted carousel editor. The device remains the source of truth. */
(function () {
  'use strict';
  const list = document.getElementById('carouselList');
  if (!list) return;
  const status = document.getElementById('carouselStatus');
  const count = document.getElementById('carouselCount');
  const refreshButton = document.getElementById('carouselRefresh');
  const panel = document.getElementById('carouselPanel');
  let saved = null;
  let busy = false;
  let operation = 0;
  let drag = null;
  let polling = false;

  function notice(message, error = false) {
    status.textContent = message;
    status.classList.toggle('is-error', error);
  }
  function validState(data) {
    if (!data || !Number.isInteger(data.speed) || data.speed < 1000 || data.speed > 60000 ||
        !Array.isArray(data.messages) || data.messages.some(message => typeof message !== 'string')) {
      throw new Error('The device returned an invalid carousel. Existing content has been kept.');
    }
    return {speed: data.speed, messages: data.messages.slice(), revision: data.revision,
      currentIndex: Number.isInteger(data.currentIndex) && data.currentIndex >= 0 && data.currentIndex < data.messages.length ? data.currentIndex : -1};
  }
  async function request(options) {
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), 12000);
    try {
      const response = await fetch('/carousel', Object.assign({cache: 'no-store'}, options, {signal: controller.signal}));
      if (!response.ok) {
        if (response.status === 423) throw new Error('The display is paused for startup or developer calibration. Try again when it is ready.');
        if (response.status === 409) throw new Error('The carousel changed on another screen. Refresh before editing again.');
        throw new Error('Could not save or load the carousel (HTTP ' + response.status + ').');
      }
      return await response.json();
    } catch (error) {
      if (error.name === 'AbortError' || error instanceof TypeError) throw new Error('The device could not be reached. Refresh to verify the saved carousel before trying again.');
      throw error;
    } finally { clearTimeout(timer); }
  }
  function setBusy(value) {
    busy = value;
    list.setAttribute('aria-busy', String(value));
    refreshButton.disabled = value;
    const add = document.getElementById('historyAddCarousel');
    const speed = document.getElementById('carouselSaveInterval');
    if (add) add.disabled = value;
    if (speed) speed.disabled = value;
    list.querySelectorAll('button').forEach(button => {
      button.disabled = value || button.dataset.unavailable === 'true';
    });
  }
  function sameList(a, b) {
    return a.length === b.length && a.every((value, index) => value === b[index]);
  }
  function makeButton(label, title, action, unavailable = false) {
    const button = document.createElement('button');
    button.type = 'button'; button.className = 'btn-secondary carousel-action';
    button.textContent = label; button.setAttribute('aria-label', title); button.title = title;
    button.dataset.unavailable = String(unavailable); button.disabled = busy || unavailable;
    button.addEventListener('click', action);
    return button;
  }
  function render() {
    list.replaceChildren();
    count.textContent = saved ? String(saved.messages.length) : '—';
    if (!saved) return;
    if (!saved.messages.length) {
      const empty = document.createElement('p'); empty.className = 'carousel-empty';
      empty.textContent = 'No carousel messages yet. Select messages in History to add them.';
      list.appendChild(empty); return;
    }
    saved.messages.forEach((text, index) => {
      const item = document.createElement('li'); item.className = 'carousel-item'; item.dataset.index = String(index);
      const heading = document.createElement('div'); heading.className = 'carousel-item-heading';
      const handle = makeButton('⠿', 'Drag message ' + (index + 1) + ' to reorder', () => {});
      handle.classList.add('carousel-drag-handle');
      handle.addEventListener('pointerdown', event => startDrag(event, handle, item, index));
      const number = document.createElement('span'); number.className = 'carousel-number'; number.textContent = 'Message ' + (index + 1);
      if (saved.currentIndex === index) {
        const playing = document.createElement('span'); playing.className = 'carousel-playing'; playing.textContent = 'Playing';
        number.appendChild(playing);
      }
      const actions = document.createElement('div'); actions.className = 'carousel-item-actions';
      actions.appendChild(makeButton('↑', 'Move message ' + (index + 1) + ' up', () => move(index, index - 1), index === 0));
      actions.appendChild(makeButton('↓', 'Move message ' + (index + 1) + ' down', () => move(index, index + 1), index === saved.messages.length - 1));
      actions.appendChild(makeButton('Delete', 'Delete message ' + (index + 1) + ' from carousel', () => remove(index)));
      heading.appendChild(handle); heading.appendChild(number); heading.appendChild(actions);
      const preview = document.createElement('pre'); preview.className = 'carousel-preview';
      preview.textContent = text.replace(/\\n/g, '\n') || '(Blank message)';
      item.appendChild(heading); item.appendChild(preview); list.appendChild(item);
    });
  }
  async function refresh(silent = false) {
    if (busy || drag || (silent && polling)) return false;
    const run = ++operation;
    if (silent) polling = true;
    else { setBusy(true); notice('Loading carousel…'); }
    try {
      const current = validState(await request());
      if (run !== operation || drag) return false;
      const changed = !saved || !sameList(saved.messages, current.messages) || saved.currentIndex !== current.currentIndex;
      saved = current; if (changed) render();
      if (!silent) notice(current.messages.length + ' message(s) · ' + (current.speed / 1000) + ' min each · Changes save automatically.');
      return true;
    } catch (error) { if (run === operation) notice(error.message, true); return false; }
    finally { if (silent) polling = false; else if (run === operation) setBusy(false); }
  }
  async function change(transform, message, checkSnapshot = true) {
    if (busy || drag) return false;
    const original = saved;
    ++operation; setBusy(true); notice('Saving carousel…');
    let posted = false;
    try {
      const current = validState(await request());
      if (checkSnapshot && (!original || !sameList(original.messages, current.messages))) {
        saved = current; render();
        notice('The carousel changed on another screen. The latest list is shown; repeat your edit.', true);
        return false;
      }
      const next = transform(current);
      if (!next) { saved = current; render(); notice(message); return true; }
      if (next.messages.length > 64) throw new Error('A carousel can contain up to 64 messages. Delete an existing message before adding more.');
      const payload = {speed: next.speed, messages: next.messages};
      if (current.revision !== undefined) payload.expectedRevision = current.revision;
      posted = true;
      await request({method: 'POST', headers: {'Content-Type': 'application/json'}, body: JSON.stringify(payload)});
      const confirmed = validState(await request());
      saved = confirmed; render();
      if (confirmed.speed !== next.speed || !sameList(confirmed.messages, next.messages)) {
        notice('The saved carousel differs from this change. The latest device list is shown; check it before editing again.', true);
        return false;
      }
      notice(message); return true;
    } catch (error) {
      // Never clear the last confirmed list or claim success after a failed write.
      render(); notice(error.message + (posted ? ' Refresh to check whether the change reached the device.' : ''), true);
      return false;
    } finally { setBusy(false); }
  }
  async function move(from, to) {
    if (!saved || from === to || from < 0 || to < 0 || from >= saved.messages.length || to >= saved.messages.length) return false;
    const restoreFocus = list.contains(document.activeElement);
    const result = await change(current => {
      const messages = current.messages.slice();
      messages.splice(to, 0, messages.splice(from, 1)[0]);
      return {speed: current.speed, messages};
    }, 'Carousel order saved.');
    if (restoreFocus) focusMessage(result ? to : from);
    return result;
  }
  async function remove(index) {
    if (!saved || index < 0 || index >= saved.messages.length) return false;
    const restoreFocus = list.contains(document.activeElement);
    const result = await change(current => ({speed: current.speed, messages: current.messages.filter((_, position) => position !== index)}), 'Message removed from the carousel. History has been kept.');
    if (restoreFocus) focusMessage(index);
    return result;
  }
  function focusMessage(index) {
    const row = saved && saved.messages.length ? list.children[Math.min(index, saved.messages.length - 1)] : null;
    const target = row ? row.querySelectorAll('button')[0] : refreshButton;
    if (target) target.focus({preventScroll: true});
  }
  async function add(messages) {
    if (!Array.isArray(messages) || messages.length === 0) { notice('Select one or more messages in History first.', true); return false; }
    let added = 0;
    const result = await change(current => {
      const combined = current.messages.slice();
      messages.forEach(text => { if (typeof text === 'string' && !combined.includes(text)) { combined.push(text); added++; } });
      return added ? {speed: current.speed, messages: combined} : null;
    }, 'Selected messages are already in the carousel.', false);
    if (result && added) notice(added + ' message(s) added at the end. Existing messages and order have been kept.');
    return result;
  }
  async function saveInterval(minutes) {
    const value = Number(minutes);
    if (!Number.isInteger(value) || value < 1 || value > 60) { notice('Choose a duration from 1 to 60 minutes.', true); return false; }
    return change(current => ({speed: value * 1000, messages: current.messages.slice()}), 'Carousel duration saved: ' + value + ' min per message.', false);
  }
  function startDrag(event, handle, item, index) {
    if (busy || (event.button !== undefined && event.button !== 0) || saved.messages.length < 2) return;
    event.preventDefault();
    drag = {pointerId: event.pointerId, handle, item, index, startY: event.clientY, clientY: event.clientY, moved: false, scrollTimer: 0};
    // Keep capture on the stable list while its child cards move in the DOM.
    list.setPointerCapture(event.pointerId); item.classList.add('is-dragging');
    drag.scrollTimer = setInterval(() => {
      if (!drag || !drag.moved) return;
      const edge = 70;
      const distance = drag.clientY < edge ? -12 : drag.clientY > window.innerHeight - edge ? 12 : 0;
      if (distance) { window.scrollBy(0, distance); placeDragged(drag.clientY); }
    }, 30);
    notice('Drag to a new position, then release to save.');
  }
  function placeDragged(y) {
    if (!drag) return;
    const siblings = Array.from(list.children).filter(item => item !== drag.item);
    const before = siblings.find(item => { const box = item.getBoundingClientRect(); return y < box.top + box.height / 2; });
    list.insertBefore(drag.item, before || null);
  }
  function moveDrag(event) {
    if (!drag || event.pointerId !== drag.pointerId) return;
    event.preventDefault(); drag.clientY = event.clientY;
    if (Math.abs(event.clientY - drag.startY) > 4) drag.moved = true;
    if (drag.moved) placeDragged(event.clientY);
  }
  function releaseDrag() {
    const previous = drag; drag = null;
    if (previous) {
      clearInterval(previous.scrollTimer); previous.item.classList.remove('is-dragging');
      if (list.hasPointerCapture(previous.pointerId)) list.releasePointerCapture(previous.pointerId);
    }
    return previous;
  }
  async function finishDrag(event) {
    if (!drag || event.pointerId !== drag.pointerId) return;
    const target = Array.from(list.children).indexOf(drag.item);
    const previous = releaseDrag(); render();
    if (previous.moved && target !== previous.index) return move(previous.index, target);
    else notice('Order unchanged. Use the arrow buttons or drag a handle to reorder.');
  }
  function cancelDrag() { releaseDrag(); render(); notice('Reorder cancelled. The saved order has been kept.'); }
  list.addEventListener('pointermove', moveDrag);
  list.addEventListener('pointerup', finishDrag);
  list.addEventListener('pointercancel', cancelDrag);
  list.addEventListener('lostpointercapture', () => { if (drag) cancelDrag(); });
  refreshButton.addEventListener('click', () => refresh());
  window.YueliCarousel = {refresh, add, saveInterval};
  window.addEventListener('load', () => refresh());
  if (panel) panel.addEventListener('toggle', () => { if (panel.open) refresh(); });
  setInterval(() => {
    const home = document.getElementById('view-home');
    if (!document.hidden && panel && panel.open && home && !home.classList.contains('hidden') && !list.contains(document.activeElement)) refresh(true);
  }, 10000);
})();
