//Grid Configuration Constants
const GRID_COLS = 23;
const GRID_ROWS = 6;

//Various variables
var carouselInterval = 5;
var unitCount = GRID_COLS;
var rowCount = GRID_ROWS;
var timezoneOffset = 0;

//Used for submission!
const form = document.getElementById('form');
form.onsubmit = function (e) {
	// An open text draft is applied before submitting, so the visible edit is sent.
	if (document.querySelector('input[name="deviceMode"]:checked').value === 'text' && textEditorDirty && !document.getElementById('textEditPanel').classList.contains('hidden')) {
		if (!applyTextEdits(false)) {
			e.preventDefault();
			return false;
		}
	}
	// If we haven't processed history yet and it is text mode, prevent default
	const deviceMode = document.querySelector('input[name="deviceMode"]:checked').value;

	// We need to handle the history save *before* the form submits (reloads)
	if (deviceMode === 'text' && !form.dataset.historySaved) {
		e.preventDefault(); // Stop submission

		var inputTextValue = document.getElementById('inputText').value;
		if (inputTextValue.trim().length > 0) {
			// Async Save
			fetch('/history')
				.then(res => res.json())
				.catch(() => []) // Fallback
				.then(history => {
					// Filter existing (check both string and object.text)
					history = history.filter(item => {
						const t = typeof item === 'string' ? item : item.text;
						return t !== inputTextValue;
					});

					// Add new Object
					history.unshift({
						text: inputTextValue,
						timestamp: Date.now(),
						tag: null // Manual tag
					});

					// Limit
					if (history.length > 50) history = history.slice(0, 50);

					// Save
					return fetch('/history', {
						method: 'POST',
						body: JSON.stringify(history)
					});
				})
				.finally(() => {
					// Continue submission
					form.dataset.historySaved = "true";
					form.submit();
				});
		} else {
			form.dataset.historySaved = "true";
			form.submit();
		}
		return false; // Stop current sync submit
	}

	//Show loading icon
	var containerSubmit = document.getElementById('containerSubmit');

	const loadingIconContainer = document.createElement("div");
	loadingIconContainer.className = "lds-facebook";

	for (var index = 0; index < 3; index++) {
		loadingIconContainer.appendChild(document.createElement("div"));
	}

	containerSubmit.replaceWith(loadingIconContainer);

	{
		const tzOffset = timezoneOffset * 60000;

		switch (deviceMode) {
			case "text":
				//Convert characters which don't translate directly, replaces ä, ö, ü with unused unicode characters #, $, &
				var inputTextValue = document.getElementById('inputText').value;

				// (History Logic Handled Async Above)

				inputTextValue = inputTextValue.replace(/ä/gi, '$');
				inputTextValue = inputTextValue.replace(/ö/gi, '&');
				inputTextValue = inputTextValue.replace(/ü/gi, '#');
				// Convert real newlines to literal \n for firmware compatibility
				inputTextValue = inputTextValue.replace(/\n/g, '\\n');
				document.getElementById('inputText').value = inputTextValue;

				//Set the hidden date time to UNIX
				var currentScheduledDateTimeText = document.getElementById('inputScheduledDateTime').value;

				//Take into account the timezone offset when we generate the unix timestamp
				var currentScheduledDateTime = new Date(currentScheduledDateTimeText);
				var time = Math.floor((currentScheduledDateTime.getTime() - tzOffset) / 1000);
				document.getElementById('inputHiddenScheduledDateTimeUnix').value = time;

				break;
			case "countdown":
				//Set the hidden date time to UNIX
				var currentCountdownDateTimeText = document.getElementById('inputCountdownDateTime').value;

				//Take into account the timezone offset when we generate the unix timestamp
				var currentCountdownDateTime = new Date(currentCountdownDateTimeText);
				var time = Math.floor((currentCountdownDateTime.getTime() - tzOffset) / 1000);
				document.getElementById('inputHiddenCountdownDateTimeUnix').value = time;

				break;
		}
	}
}

// Retrieve current Split-Flap settings when the page loads/refreshes
window.addEventListener('load', loadPage);

// Request and retrieve settings from ESP-01s filesystem
function loadPage() {
	//Show messages from the server if need be
	const urlParams = new URLSearchParams(location.search);
	if (urlParams.get('invalid-submission') === "true") {
		showBannerMessage(`
			Something went wrong during submission. Feel free to try again, ensure that you have entered valid information.
			<br>
			Ensure things like dates provided for schedules/countdowns are in the future.
		`);
	}
	else if (urlParams.get('is-resetting-units') === "true") {
		showBannerMessage(`
			Display is now resetting/re-calibrating. It should only take a few seconds.
			<br>
			It will display different characters in order to carry this out and then go back to the last thing being displayed.
		`);
	}

	//Set date time fields to be a minimum of todays date/time add 1 minute
	var tzOffset = timezoneOffset * 60000;
	document.querySelectorAll('input[type="datetime-local"]').forEach((dateTimeElement) => {
		var currentDateTime = convertDateToString((new Date(Date.now() - tzOffset + 60000)));
		dateTimeElement.value = dateTimeElement.min = currentDateTime;
	});

	{
		var xhrRequest = new XMLHttpRequest();
		xhrRequest.onreadystatechange = function () {
			if (this.readyState == 4) {
				if (this.status == 200) {
					var responseObject = JSON.parse(this.responseText);

					timezoneOffset = responseObject.timezoneOffset;

					setSpeed(responseObject.flapSpeed);
					setSavedMode(responseObject.deviceMode);

					setVersion(responseObject.version);
					setUnitCount(responseObject.unitCount, responseObject.rowCount);
					setCountdownDate(responseObject.countdownToDateUnix);
					setLastReceivedMessage(responseObject.lastTimeReceivedMessageDateTime);
					showHideResetWifiSettingsAction(responseObject.wifiSettingsResettable);
					showHideOtaUpdateAction(responseObject.otaEnabled);

					// Carousel Speed
					if (responseObject.carouselSpeed) {
						carouselInterval = Math.round(responseObject.carouselSpeed / 1000);
						updateCarouselInterval(carouselInterval);
					}

					showContent();
				} else {
					// Handle error
					console.error("Connection failed with status:", this.status);
					showBannerMessage("Connection Error: " + this.status + ". Ensure you are connected to the device WiFi.", false);
					// Attempt to show content anyway so user isn't stuck
					showContent();
				}
			}
		};

		xhrRequest.onerror = function () {
			console.error("Network Error");
			showBannerMessage("Network Error: Could not connect to device.", false);
			showContent();
		};

		xhrRequest.open("GET", "/settings", true);
		xhrRequest.send();
	}
	initVirtualKeyboard();

	// Hook up auto-formatting for the text input
	// (Replaced by Grid Logic)
	initDisplayGrid();
	initPhysicalKeyboard();
}

// Shows a message up top of the page should the server request one to be shown
function showBannerMessage(message, hideAfterDuration) {
	var bannerMessageElement = document.getElementById('bannerMessage');
	bannerMessageElement.innerHTML = message;

	bannerMessageElement.classList.remove("hidden");

	if (hideAfterDuration) {
		setTimeout(function () {
			bannerMessageElement.classList.add("hidden");
		}, 7500);
	}
}

//Ongoing show how many characters are being used
function updateCharacterCount() {
	var count = 0;
	var usedLines = 0;
	// Rows are fixed-width (padded with spaces); count non-space chars
	for (let r = 0; r < rowCount; r++) {
		const line = gridLines[r] || "";
		for (let c = 0; c < line.length; c++) {
			if (line[c] !== " ") count++;
		}
		if (line.trim().length > 0) usedLines++;
	}

	var labelCharacterCount = document.getElementById("labelCharacterCount");
	var labelLineCount = document.getElementById("labelLineCount");

	labelCharacterCount.innerHTML = count;
	labelLineCount.innerHTML = usedLines;
}

// Global Grid State
// gridLines: Fixed-width strings, one per row (space padded to unitCount),
// so every cell has an addressable position.
let gridLines = [];
let selectedRow = 0;
let selectedCol = 0;
let gridCells = [];
let renderedGridLines = [];
let selectedGridCell = null;

function initDisplayGrid() {
	const grid = document.getElementById('displayGrid');
	if (!grid) return;

	grid.innerHTML = '';
	gridLines = [];
	gridCells = [];
	renderedGridLines = [];
	selectedGridCell = null;
	for (let i = 0; i < rowCount; i++) gridLines.push(" ".repeat(unitCount));

	selectedRow = 0;
	selectedCol = 0;

	for (let i = 0; i < rowCount; i++) {
		const rowDiv = document.createElement('div');
		rowDiv.className = 'display-row';
		const rowCells = [];

		for (let j = 0; j < unitCount; j++) {
			const cell = document.createElement('button');
			cell.type = 'button';
			cell.setAttribute('aria-label', `Row ${i + 1}, column ${j + 1}`);
			cell.setAttribute('aria-pressed', 'false');
			cell.className = 'display-cell';
			cell.id = `cell-${i}-${j}`;
			// Clicking a cell selects that exact position
			cell.onclick = () => selectCell(i, j);
			rowDiv.appendChild(cell);
			rowCells.push(cell);
		}
		gridCells.push(rowCells);
		grid.appendChild(rowDiv);
	}
	renderDisplayGrid();
}

function selectCell(r, c) {
	selectedRow = r;
	selectedCol = c;
	renderDisplayGrid();
}

function renderDisplayGrid() {
	const rowsOut = [];

	for (let i = 0; gridLines && i < rowCount; i++) {
		// Rows are fixed-width already; keep them addressable cell by cell
		let lineContent = (gridLines[i] || "").slice(0, unitCount);
		while (lineContent.length < unitCount) lineContent += " ";

		for (let j = 0; j < unitCount; j++) {
			if (renderedGridLines[i] && renderedGridLines[i][j] === lineContent[j]) continue;
			const cell = gridCells[i] && gridCells[i][j];
			const char = lineContent[j];

			if (cell) {
				// Check for color symbol
				const color = getColorFromSymbol(char);
				if (color) {
					cell.textContent = "";
					cell.style.backgroundColor = color;
					// Add border if it's the white/light color to make it visible
					if (char === "●" || char === "▲") {
						cell.style.border = "1px solid #ccc";
					} else {
						cell.style.border = "1px solid #444"; // Default
					}
				} else {
					cell.style.backgroundColor = ""; // Reset
					cell.style.border = ""; // Reset
					cell.textContent = char;
				}
			}
		}

		rowsOut.push(lineContent);
	}

	renderedGridLines = rowsOut;
	const nextSelectedCell = gridCells[selectedRow] && gridCells[selectedRow][selectedCol];
	if (nextSelectedCell !== selectedGridCell) {
		if (selectedGridCell) {
			selectedGridCell.setAttribute('aria-pressed', 'false');
			selectedGridCell.classList.remove('selected');
		}
		if (nextSelectedCell) {
			nextSelectedCell.setAttribute('aria-pressed', 'true');
			nextSelectedCell.classList.add('selected');
		}
		selectedGridCell = nextSelectedCell;
	}
	const input = document.getElementById('inputText');
	const text = rowsOut.join('\n');
	if (input.value !== text) input.value = text;
}



//Updates slider value while sliding
function updateSpeedSlider() {
	var sliderValue = document.getElementById("rangeFlapSpeed").value;
	document.getElementById("rangeFlapSpeedValue").innerHTML = sliderValue + " %";
}

//Sets mode by checking corresponding radio button/tab
function setSavedMode(mode) {
	switch (mode) {
		case "text":
			document.getElementById("modeText").checked = true;
			break;
		case "date":
			document.getElementById("modeDate").checked = true;
			break;
		case "clock":
			document.getElementById("modeClock").checked = true;
			break;
		case "countdown":
			document.getElementById("modeCountdown").checked = true;
			break;
		case "carousel":
			document.getElementById("modeCarousel").checked = true;
			break;
	}

	setDeviceModeTab(mode);
}

//Shows/hides the tab associated with the device mode
function setDeviceModeTab(mode) {
	document.querySelectorAll('.tab').forEach(function (tab) {
		if (!tab.classList.contains("hidden")) {
			tab.classList.add("hidden");
		}
	});

	var tabName = `tab-${mode}`;
	var tab = document.getElementById(tabName);
	if (tab !== null) {
		tab.classList.remove("hidden");
	}
}

//Sets flap speed by setting the ranges
function setSpeed(speed) {
	document.getElementById("rangeFlapSpeedValue").innerHTML = speed + " %";
	document.getElementById("rangeFlapSpeed").value = speed;
}


//Sets the version on the UI just for awareness
function setVersion(version) {
	document.getElementById("labelVersion").textContent =
		typeof version === "string" && version.trim() ? version : "N/A";
}

//Sets the version on the UI just for awareness
function setUnitCount(count, rows) {
	document.getElementById("labelUnits").innerHTML = count + " x " + (rows || 1);
	unitCount = parseInt(count);
	rowCount = parseInt(rows || 1);

	// Update max length, allow extra chars for newlines
	var maxLen = (unitCount * rowCount) + (rowCount - 1);
	document.getElementById('inputText').maxLength = maxLen;
}

//Sets the version on the UI just for awareness
function setCountdownDate(dateUnix) {
	//Set date fields to be a minimum of tomorrows date
	var currentCountdownDate = document.getElementById('inputCountdownDateTime');
	var tzOffset = timezoneOffset * 60000;

	var currentDate = (new Date(Date.now() - tzOffset));
	var nextDayDate = new Date();
	nextDayDate.setDate(currentDate.getDate() + 1);

	//If one has been set and it is not exceeded
	if (dateUnix !== 0) {
		var countdownDate = new Date(dateUnix * 1000);
		if (countdownDate >= currentDate) {
			currentCountdownDate.value = convertDateToString(countdownDate);

			if (countdownDate - currentDate < 24000) {
				currentCountdownDate.min = convertDateToString(nextDayDate);
				return;
			}
		}
	}
	else {
		//Set date fields to be a minimum of tomorrows date	
		currentCountdownDate.value = convertDateToString(nextDayDate);
	}

	currentCountdownDate.min = convertDateToString(nextDayDate);
}

function convertDateToString(dateTime) {
	const year = dateTime.getFullYear();
	const month = String(dateTime.getMonth() + 1).padStart(2, '0');
	const day = String(dateTime.getDate()).padStart(2, '0');

	const hours = String(dateTime.getHours()).padStart(2, '0');
	const minutes = String(dateTime.getMinutes()).padStart(2, '0');

	return `${year}-${month}-${day}T${hours}:${minutes}`;
}

//Sets the last received post message to the server
function setLastReceivedMessage(time) {
	const timeMessage = typeof time === "string" && time.trim() ? time : "N/A";
	document.getElementById("labelLastMessageReceived").textContent = timeMessage;
}

//Used for scheduling messages
function showHideScheduledMessageInput() {
	var scheduleOptionsElement = document.getElementById("divScheduleOptions");
	var checkboxScheduled = document.getElementById("inputCheckboxScheduleEnabled");

	if (checkboxScheduled.checked) {
		scheduleOptionsElement.classList.remove("hidden")
	}
	else {
		scheduleOptionsElement.classList.add("hidden")
	}
}

function showHideResetWifiSettingsAction(isWifiApMode) {
	if (!isWifiApMode) {
		var linkActionResetWifi = document.getElementById("linkActionResetWifi");
		linkActionResetWifi.classList.add("hidden");
	}
}

function showHideOtaUpdateAction(isOtaEnabled) {
	if (!isOtaEnabled) {
		var linkActionOtaUpdate = document.getElementById("linkActionOtaUpdate");
		linkActionOtaUpdate.classList.add("hidden");
	}
}


function showContent() {
	var elementInitialLoading = document.getElementById("initialLoading");
	var elementContent = document.getElementById("loadedContent");

	elementContent.classList.remove("hidden");
}

// On-screen QWERTY keyboard (all 64 flap characters, no IME needed)

// Keyboard layout: the 64-char flap set arranged like a physical keyboard
const KB_ROWS = [
	["1", "2", "3", "4", "5", "6", "7", "8", "9", "0"],
	["Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P"],
	["A", "S", "D", "F", "G", "H", "J", "K", "L"],
	["?", "Z", "X", "C", "V", "B", "N", "M", "!"],
	[":", "-", "$", "@", "#", "&", ";", "(", ")", "+", "="],
	["'", "\"", "/", "°", "%", ".", ",", "ENTER", "BACK"],
	["SPACE", "●", "◆", "★", "▲", "■", "▼", "○"]
];

// Color / symbol keys share the flap color scheme
const KB_COLOR_CHARS = ["●", "◆", "★", "▲", "■", "▼", "○"];

function initVirtualKeyboard() {
	const container = document.getElementById('virtualKeyboard');
	if (!container) return;

	container.innerHTML = '';

	KB_ROWS.forEach((rowChars, rowIndex) => {
		const rowDiv = document.createElement('div');
		rowDiv.className = 'kb-row';
		if (rowIndex === 2) rowDiv.classList.add('kb-row-home');
		if (rowIndex === 3) rowDiv.classList.add('kb-row-bottom');

		rowChars.forEach(char => {
			// Function keys first
			if (char === "ENTER" || char === "BACK") {
				const keyDiv = document.createElement('button');
				keyDiv.type = 'button';
				keyDiv.dataset.key = char;
				keyDiv.setAttribute('aria-label', char === "ENTER" ? 'Enter, next row' : 'Backspace');
				keyDiv.className = 'key key-func';
				keyDiv.innerText = (char === "ENTER") ? 'Enter ↵' : '⌫';
				keyDiv.onclick = (char === "ENTER") ? handleEnter : handleBackspace;
				rowDiv.appendChild(keyDiv);
				return;
			}

			if (KB_COLOR_CHARS.includes(char)) {
				// Color flap key: colored circle
				const keyDiv = document.createElement('button');
				keyDiv.type = 'button';
				keyDiv.dataset.key = char;
				keyDiv.className = 'key key-color';
				keyDiv.setAttribute('translate', 'no');
				keyDiv.classList.add('notranslate');
				keyDiv.style.setProperty('--key-color', getColorFromSymbol(char));
				const colorName = { "●": "White", "◆": "Red", "★": "Orange", "▲": "Gold", "■": "Green", "▼": "Blue", "○": "Purple" }[char];
				keyDiv.setAttribute('aria-label', `${colorName} color`);
				keyDiv.title = colorName;
				if (char === "●") {
					keyDiv.classList.add('key-color-white');
					keyDiv.title = "White";
				}
				keyDiv.onclick = function () { handleKey(char); };
				rowDiv.appendChild(keyDiv);
				return;
			}

			// Regular character key (SPACE becomes a wide key)
			const keyDiv = document.createElement('button');
			keyDiv.type = 'button';
			keyDiv.dataset.key = char;
			keyDiv.setAttribute('aria-label', char === "SPACE" ? 'Space' : char);
			keyDiv.className = 'key';
			if (char === "SPACE") {
				keyDiv.classList.add('key-space');
				keyDiv.innerText = 'Space';
			} else {
				keyDiv.innerText = char;
			}
			keyDiv.setAttribute('translate', 'no');
			keyDiv.classList.add('notranslate');
			keyDiv.onclick = function () { handleKey(char); };
			rowDiv.appendChild(keyDiv);
		});
		container.appendChild(rowDiv);
	});
}

// Writes a char into the selected cell, then moves selection right
function handleKey(char) {
	if (!gridLines.length) return;
	const charToWrite = (char === "SPACE") ? " " : char;

	let line = gridLines[selectedRow] || "";
	while (line.length < unitCount) line += " ";
	line = line.substring(0, selectedCol) + charToWrite + line.substring(selectedCol + 1);
	gridLines[selectedRow] = line;

	// Advance right, stop at line end
	if (selectedCol < unitCount - 1) selectedCol++;

	renderDisplayGrid();
	updateCharacterCount();
}

// Clears the selected cell and moves selection left
function handleBackspace() {
	if (!gridLines.length) return;
	let line = gridLines[selectedRow];
	line = line.substring(0, selectedCol) + " " + line.substring(selectedCol + 1);
	gridLines[selectedRow] = line;

	if (selectedCol > 0) selectedCol--;

	renderDisplayGrid();
	updateCharacterCount();
}

// Moves selection to the start of the next row
function handleEnter() {
	if (selectedRow < rowCount - 1) {
		selectedRow++;
		selectedCol = 0;
	}
	renderDisplayGrid();
}

// Clipboard operations also work on the device's HTTP page through native
// copy/paste events and an ordinary textarea when the Clipboard API is absent.
let textEditorDirty = false;
let textEditorRevision = 0;
let physicalKeyboardInitialized = false;

function getBoardText() {
	const lines = gridLines.map(line => line.replace(/ +$/, ''));
	while (lines.length && lines[lines.length - 1] === '') lines.pop();
	return lines.join('\n');
}

function normalizeBoardText(text) {
	const allowed = new Set(KB_ROWS.flat().filter(key => key !== 'ENTER' && key !== 'BACK' && key !== 'SPACE'));
	allowed.add(' ');
	let unsupported = 0;
	const normalized = Array.from(String(text).replace(/\r\n?/g, '\n').normalize('NFKC')).map(char => {
		if (char === '\n') return '\n';
		if (char === '\t') return '    ';
		if ('‘’'.includes(char)) return "'";
		if ('“”'.includes(char)) return '"';
		if ('–—'.includes(char)) return '-';
		const upper = char.toUpperCase();
		if (allowed.has(upper)) return upper;
		unsupported++;
		return ' ';
	}).join('');
	return { text: normalized, unsupported };
}

function layoutBoardText(text, startRow = 0, startCol = 0, replace = false) {
	const normalized = normalizeBoardText(text);
	const lines = replace ? Array(rowCount).fill(' '.repeat(unitCount)) : gridLines.slice();
	let row = startRow;
	let col = startCol;
	let clipped = false;
	for (const char of normalized.text) {
		if (char === '\n') {
			row++;
			col = 0;
			continue;
		}
		if (col >= unitCount) { row++; col = 0; }
		if (row >= rowCount) { clipped = true; break; }
		lines[row] = lines[row].slice(0, col) + char + lines[row].slice(col + 1);
		col++;
	}
	return { lines, row: Math.min(row, rowCount - 1), col: row >= rowCount ? unitCount - 1 : Math.min(col, unitCount - 1), unsupported: normalized.unsupported, clipped };
}

// Lay out whole words before touching the board. Explicit newlines (including
// empty rows) are kept; automatic wrapping never divides a word or punctuation.
function layoutBoardWords(text, startRow = 0, startCol = 0, replace = false, center = false) {
	const normalized = normalizeBoardText(text);
	const paragraphs = normalized.text.split('\n').map(line => line.trim().split(/ +/).filter(Boolean));
	const tooLong = paragraphs.flat().find(word => word.length > unitCount);
	if (tooLong) {
		return { unsupported: normalized.unsupported, clipped: false, error: `A word is ${tooLong.length} characters long. Shorten it to ${unitCount} characters or fewer; words cannot be split across rows.` };
	}
	const wrapped = [{ offset: startCol, text: '' }];
	paragraphs.forEach((words, paragraphIndex) => {
		if (paragraphIndex) wrapped.push({ offset: 0, text: '' });
		words.forEach(word => {
			let line = wrapped[wrapped.length - 1];
			let separator = line.text ? ' ' : '';
			if (line.offset + line.text.length + separator.length + word.length > unitCount) {
				line = { offset: 0, text: '' };
				wrapped.push(line);
				separator = '';
			}
			line.text += separator + word;
		});
	});
	const lines = replace ? Array(rowCount).fill(' '.repeat(unitCount)) : gridLines.slice();
	wrapped.forEach((line, index) => {
		const row = startRow + index;
		if (row >= rowCount || !line.text) return;
		const col = center ? Math.floor((unitCount - line.text.length) / 2) : line.offset;
		lines[row] = lines[row].slice(0, col) + line.text + lines[row].slice(col + line.text.length);
	});
	const finalLine = wrapped[wrapped.length - 1];
	const lastRow = startRow + wrapped.length - 1;
	const clipped = lastRow >= rowCount;
	return { lines, row: Math.min(lastRow, rowCount - 1), col: clipped ? unitCount - 1 : Math.min(finalLine.offset + finalLine.text.length, unitCount - 1), unsupported: normalized.unsupported, clipped };
}

function layoutEditorText() {
	const text = document.getElementById('messageEditor').value;
	const autoLayout = document.getElementById('editorAutoLayout');
	return (!autoLayout || autoLayout.checked) ? layoutBoardWords(text, 0, 0, true, true) : layoutBoardText(text, 0, 0, true);
}

function textLayoutNotice(result) {
	const notes = [];
	if (result.error) notes.push(result.error);
	if (result.unsupported) notes.push(`${result.unsupported} unsupported character(s) replaced with spaces.`);
	if (result.clipped) notes.push(`Only the first ${rowCount} rows fit on the board.`);
	return notes.join(' ');
}

function insertBoardText(text) {
	const result = layoutBoardWords(text, selectedRow, selectedCol);
	if (result.error) {
		showToast(textLayoutNotice(result));
		return false;
	}
	gridLines = result.lines;
	selectedRow = result.row;
	selectedCol = result.col;
	renderDisplayGrid();
	updateCharacterCount();
	const notice = textLayoutNotice(result);
	if (notice) showToast(notice);
	return true;
}

function openTextEditor() {
	textEditorRevision++;
	const panel = document.getElementById('textEditPanel');
	const editor = document.getElementById('messageEditor');
	if (panel.classList.contains('hidden')) {
		editor.value = getBoardText();
		textEditorDirty = false;
	}
	panel.classList.remove('hidden');
	document.getElementById('textEditStatus').innerText = 'Type or paste text, then apply. This replaces the entire board.';
	editor.focus();
	editor.select();
}

function updateTextEditStatus() {
	textEditorRevision++;
	textEditorDirty = true;
	const result = layoutEditorText();
	const notice = textLayoutNotice(result);
	const autoLayout = document.getElementById('editorAutoLayout');
	const layoutLabel = (!autoLayout || autoLayout.checked) ? 'whole words, centered' : 'exact spacing';
	document.getElementById('textEditStatus').innerText = notice || `Ready to apply · ${unitCount} columns × ${rowCount} rows · ${layoutLabel} · replaces the entire board.`;
}

function closeTextEditor() {
	textEditorRevision++;
	document.getElementById('textEditPanel').classList.add('hidden');
	textEditorDirty = false;
}

function applyTextEdits(focusGrid = true) {
	const result = layoutEditorText();
	if (result.error) {
		document.getElementById('textEditStatus').innerText = textLayoutNotice(result);
		document.getElementById('messageEditor').focus();
		return false;
	}
	gridLines = result.lines;
	selectedRow = 0;
	selectedCol = 0;
	renderDisplayGrid();
	updateCharacterCount();
	closeTextEditor();
	showToast(textLayoutNotice(result) || 'Message applied to grid.');
	if (focusGrid) document.getElementById('cell-0-0').focus({ preventScroll: true });
	return true;
}

async function copyBoardText() {
	const text = getBoardText();
	try {
		if (navigator.clipboard && window.isSecureContext) {
			await navigator.clipboard.writeText(text);
			showToast('Board copied.');
			return;
		}
	} catch (_) { /* Fall back to a user-initiated native copy on HTTP. */ }
	const active = document.activeElement;
	const copyField = document.createElement('textarea');
	copyField.value = text;
	copyField.setAttribute('aria-label', 'Copy board text');
	copyField.style.cssText = 'position:fixed;left:-9999px;top:0;';
	document.body.appendChild(copyField);
	copyField.select();
	let copied = false;
	try { copied = document.execCommand('copy'); } catch (_) { /* Manual fallback below. */ }
	copyField.remove();
	if (active && active.focus) active.focus({ preventScroll: true });
	if (copied) showToast('Board copied.');
	else {
		openTextEditor();
		document.getElementById('textEditStatus').innerText = 'Text selected. Use Ctrl/Cmd+C or the selection menu to copy.';
	}
}

async function pasteBoardText() {
	openTextEditor();
	const revision = textEditorRevision;
	try {
		if (navigator.clipboard && window.isSecureContext) {
			const text = await navigator.clipboard.readText();
			if (revision !== textEditorRevision || document.getElementById('textEditPanel').classList.contains('hidden')) return;
			if (text) {
				document.getElementById('messageEditor').value = text;
				updateTextEditStatus();
			}
			return;
		}
	} catch (_) { /* Device HTTP pages use the normal textarea paste menu. */ }
	if (revision !== textEditorRevision || document.getElementById('textEditPanel').classList.contains('hidden')) return;
	document.getElementById('textEditStatus').innerText = 'Paste here with Ctrl/Cmd+V or the paste menu, edit, then Apply to grid.';
}

function isBoardKeyboardTarget(target) {
	if (!gridLines.length || document.getElementById('view-home').classList.contains('hidden') || document.getElementById('tab-text').classList.contains('hidden')) return false;
	if (!target || target.isContentEditable || target.closest('input, textarea, select, [contenteditable="true"]')) return false;
	return target === document.body || Boolean(target.closest('#displayGrid, #virtualKeyboard'));
}

function handleBoardKeydown(event) {
	if (!isBoardKeyboardTarget(event.target) || event.isComposing || event.keyCode === 229 || event.ctrlKey || event.metaKey || event.altKey) return;
	const moves = { ArrowLeft: [0, -1], ArrowRight: [0, 1], ArrowUp: [-1, 0], ArrowDown: [1, 0] };
	if (moves[event.key]) {
		event.preventDefault();
		selectedRow = Math.max(0, Math.min(rowCount - 1, selectedRow + moves[event.key][0]));
		selectedCol = Math.max(0, Math.min(unitCount - 1, selectedCol + moves[event.key][1]));
		renderDisplayGrid();
		return;
	}
	if (event.key === 'Enter' || event.key === 'Backspace') {
		event.preventDefault();
		if (event.key === 'Enter') handleEnter(); else handleBackspace();
		return;
	}
	if (event.key === 'Delete') {
		event.preventDefault();
		const line = gridLines[selectedRow];
		gridLines[selectedRow] = line.slice(0, selectedCol) + ' ' + line.slice(selectedCol + 1);
		renderDisplayGrid();
		updateCharacterCount();
		return;
	}
	if (event.key === 'Home' || event.key === 'End') {
		event.preventDefault();
		selectedCol = event.key === 'Home' ? 0 : unitCount - 1;
		renderDisplayGrid();
		return;
	}
	if (Array.from(event.key).length === 1) {
		event.preventDefault();
		const normalized = normalizeBoardText(event.key);
		if (!normalized.unsupported && normalized.text.length === 1) handleKey(normalized.text);
		else showToast('Use the characters shown on the keyboard.');
	}
}

function initPhysicalKeyboard() {
	if (physicalKeyboardInitialized) return;
	physicalKeyboardInitialized = true;
	document.addEventListener('keydown', handleBoardKeydown);
	document.addEventListener('paste', event => {
		if (!isBoardKeyboardTarget(event.target) || !event.clipboardData) return;
		const text = event.clipboardData.getData('text/plain');
		if (!text) return;
		event.preventDefault();
		insertBoardText(text);
	});
	document.addEventListener('copy', event => {
		if (!isBoardKeyboardTarget(event.target) || !event.clipboardData || window.getSelection().toString()) return;
		event.clipboardData.setData('text/plain', getBoardText());
		event.preventDefault();
	});
}

// Helper to map symbol to color
function getColorFromSymbol(char) {
	switch (char) {
		case "●": return "#FFFFFF"; // Pure White
		case "◆": return "red";
		case "★": return "#F8880E"; // Orange
		case "▲": return "gold";
		case "■": return "green";
		case "▼": return "blue";
		case "○": return "purple";
		default: return null;
	}
}


// --- NEW PAGE LOGIC ---

function switchView(viewName) {
	// Hide all views
	document.querySelectorAll('.view').forEach(el => el.classList.add('hidden'));

	// Show selected
	const viewId = `view-${viewName}`;
	const selectedView = document.getElementById(viewId);
	if (selectedView) {
		selectedView.classList.remove('hidden');
	}

	if (viewName === 'history') {
		renderHistory();
	}
	if (viewName === 'home' && window.YueliCarousel) {
		window.YueliCarousel.refresh();
	}

	// Update Bottom Nav
	document.querySelectorAll('.bottom-nav .nav-item').forEach(el => el.classList.remove('active'));
	// Find link
	const links = document.querySelectorAll('.bottom-nav .nav-item');
	if (viewName === 'home') links[0].classList.add('active');
	if (viewName === 'history') links[1].classList.add('active');
}

function showCarouselInformation() {
	switchView('home');
	const panel = document.getElementById('carouselPanel');
	if (panel) {
		panel.open = true;
		panel.scrollIntoView({ behavior: 'smooth', block: 'start' });
	}
}


// --- HISTORY ---
// (No more category navigation, just flat list with headers)

function renderHistory() {
	const list = document.getElementById('history-list');
	if (!list) return;
	list.innerHTML = 'Loading...';

	fetch('/history')
		.then(res => res.json())
		.catch(() => [])
		.then(history => {
			list.innerHTML = '';

			if (!Array.isArray(history) || history.length === 0) {
				list.innerHTML = '<p style="grid-column: span 2; text-align:center; color:#888;">No history yet.</p>';
				return;
			}

			// Normalization
			const items = history.map(item => {
				if (typeof item === 'string') return { text: item, timestamp: 0, tag: null };
				return item;
			});

			// Group by Time
			const groups = { 'Today': [], 'Yesterday': [], 'Earlier': [] };
			items.forEach(item => {
				const cat = getTimeCategory(item.timestamp);
				if (groups[cat]) groups[cat].push(item);
				else groups['Earlier'].push(item); // Fallback
			});

			// Render Groups
			for (const [sectionName, sectionItems] of Object.entries(groups)) {
				if (sectionItems.length === 0) continue;

				// Section Header
				const header = document.createElement('h3');
				header.className = 'history-section-header';
				header.innerText = sectionName;
				list.appendChild(header);

				// Grid Container for items
				const gridContainer = document.createElement('div');
				gridContainer.className = 'history-grid-container'; // New class for layout

				sectionItems.forEach(item => {
					const card = createHistoryGridCard(item);
					gridContainer.appendChild(card);
				});

				list.appendChild(gridContainer);
			}
		});
}

function createHistoryGridCard(item) {
	const wrapper = document.createElement('div');
	// Border color based on tag or random hash
	const borderColorClass = item.tag ? getBorderClass(item.tag) : getBorderClass(item.text);
	wrapper.className = `history-card-wrapper ${borderColorClass}`;

	// Add Checkbox for Carousel Selection
	const checkbox = document.createElement('input');
	checkbox.type = 'checkbox';
	checkbox.className = 'history-checkbox';
	checkbox.value = item.text;
	checkbox.setAttribute('aria-label', 'Select message for carousel');
	// Prevent triggering card click when clicking checkbox
	checkbox.onclick = (e) => e.stopPropagation();
	wrapper.appendChild(checkbox);

	// wrapper click triggers quick send, unless in "Selection Mode"?
	// User can just click card to send. Checkbox is specific.
	wrapper.onclick = (e) => {
		// If clicking anywhere else, maybe toggle checkbox? 
		// Or keep original functionality: Quick Send.
		// Let's keep Quick Send. User explicitly clicks checkbox for carousel.
		quickSend(item.text);
	};

	// Render Mini Grid
	const text = item.text || "";
	const lines = text.split('\n');

	for (let r = 0; r < rowCount; r++) {
		const rowDiv = document.createElement('div');
		rowDiv.className = 'mini-row';

		let lineContent = lines[r] || "";
		let padded = lineContent.padStart(unitCount, " ").slice(-unitCount);

		for (let c = 0; c < unitCount; c++) {
			const charDiv = document.createElement('div');
			charDiv.className = 'mini-cell';

			const char = padded[c];
			const color = getColorFromSymbol(char);
			if (color) {
				charDiv.style.backgroundColor = color;
				charDiv.innerText = "";
			} else {
				charDiv.innerText = char;
			}

			rowDiv.appendChild(charDiv);
		}
		wrapper.appendChild(rowDiv);
	}

	// Tag Label (if any)
	if (item.tag) {
		const tagLabel = document.createElement('div');
		tagLabel.className = 'mini-tag-label';
		tagLabel.innerText = item.tag;
		wrapper.appendChild(tagLabel);
	}

	return wrapper;
}

// (Old History Helpers Removed)


function getTimeCategory(ts) {
	if (!ts) return 'Earlier';
	const date = new Date(ts);
	const now = new Date();

	// Reset times to midnight covers comparison issues
	const d = new Date(date).setHours(0, 0, 0, 0);
	const n = new Date(now).setHours(0, 0, 0, 0);

	const diff = n - d;
	const oneDay = 24 * 60 * 60 * 1000;

	if (diff === 0) return 'Today';
	if (diff === oneDay) return 'Yesterday';
	return 'Earlier';
}

function quickSend(text) {
	// Post to / (simulate form) ? No, actually /?text=... or just submit the form via XHR
	// The existing form uses standard GET/POST.
	// Let's use the same logic as sync submit but via fetch
	// Actually, looking at Master_C3.ino/script.js, form submission reloads page.
	// To do it background, we need to replicate the request.
	// Assuming form is method="GET" (default). Wait, let's check index.html or script.
	// Master_C3.ino handles param input.

	// Construct params
	const params = new URLSearchParams();
	params.append('deviceMode', 'text');
	params.append('unitCount', unitCount);
	params.append('rowCount', rowCount);

	// Handle special chars replacement map (from script.js)
	let safeText = text.replace(/ä/gi, '$').replace(/ö/gi, '&').replace(/ü/gi, '#').replace(/\n/g, '\\n');
	params.append('inputText', safeText);
	params.append('scheduledDateTimeUnix', 0); // Immediate

	// We also need other settings? 
	// Usually form includes everything. Let's just send what's needed for text.

	// Actually, if we use GET to /, it returns the HTML. We just want to trigger the action.
	// Does the firmware reply 200 OK? Yes.

	// Use POST with body params, matching the form behavior
	fetch('/', {
		method: 'POST',
		body: params
	})
		.then(res => {
			if (res.ok || res.type === 'opaque') showToast("Message Sent!");
			else showToast("Error Sending");
		})
		.catch(e => showToast("Connection Error"));
}

function showToast(message) {
	const container = document.getElementById('toast-container');
	const toast = document.createElement('div');
	toast.className = 'toast';
	toast.innerText = message;
	container.appendChild(toast);

	// Animate
	requestAnimationFrame(() => toast.classList.add('show'));

	setTimeout(() => {
		toast.classList.remove('show');
		setTimeout(() => toast.remove(), 300);
	}, 2000);
}

// --- DRAFTS ---
// (Drafts functionality removed)


// --- HELPERS ---
function getBorderClass(text) {
	const colors = ['border-red', 'border-orange', 'border-yellow', 'border-green', 'border-blue', 'border-purple'];
	// Simple hash to consistency assign color to text
	let hash = 0;
	for (let i = 0; i < text.length; i++) {
		hash = text.charCodeAt(i) + ((hash << 5) - hash);
	}
	const index = Math.abs(hash) % colors.length;
	return colors[index];
}


// --- CAROUSEL FUNCTIONS ---

function updateCarouselInterval(val) {
	carouselInterval = parseInt(val);
	document.getElementById('rangeCarouselInterval').value = carouselInterval;
	document.getElementById('labelCarouselInterval').innerText = carouselInterval + 'min';
}

async function saveCarouselSettingsOnly() {
	if (!window.YueliCarousel) { showToast('Carousel controls are unavailable. Reload the page.'); return; }
	const saved = await window.YueliCarousel.saveInterval(carouselInterval);
	showToast(saved ? 'Carousel duration saved.' : 'Duration was not confirmed. Check Carousel Information.');
}

async function setCarouselFromHistory() {
	const selected = Array.from(document.querySelectorAll('.history-checkbox:checked'));
	if (!selected.length) { showToast('Select one or more messages first.'); return; }
	if (!window.YueliCarousel) { showToast('Carousel controls are unavailable. Reload the page.'); return; }
	const saved = await window.YueliCarousel.add(selected.map(checkbox => checkbox.value));
	if (saved) {
		selected.forEach(checkbox => { checkbox.checked = false; });
		showToast('Selected messages are in the carousel. Existing messages have been kept.');
		showCarouselInformation();
	} else showToast('Changes were not confirmed. Your selection has been kept.');
}
