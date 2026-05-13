const $ = id => document.getElementById(id);
const esc = s => s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/"/g, '&quot;');
const getSaved = ssid => savedNetworks.includes(ssid);

const errorMessages = {
    'No data': 'Nesaņemti dati. Lūdzu, mēģiniet vēlreiz.',
    'Invalid JSON': 'Kļūdains pieprasījums. Lūdzu, mēģiniet vēlreiz.',
    'SSID cannot be empty': 'Tīkla nosaukums nevar būt tukšs.',
    'SSID too long': 'Tīkla nosaukums ir pārāk garš (maks. 32 simboli).',
    'Password too long': 'Parole ir pārāk gara (maks. 63 simboli).',
    'SSID required': 'Tīkla nosaukums ir nepieciešams.',
    '_network': 'Neizdevās sazināties ar ierīci. Lūdzu, pārbaudiet savienojumu un mēģiniet vēlreiz.',
    '_unknown': 'Nezināma kļūda. Lūdzu, mēģiniet vēlreiz.',
};

let savedNetworks = [];
let pwField, pwHead, editBtn, subBtn, statusEl, actionMsgEl, pollStatusEl;

function showTempStatus(html) {
    if (actionMsgEl._clearTimer) {
        clearTimeout(actionMsgEl._clearTimer);
        actionMsgEl._clearTimer = null;
    }
    actionMsgEl.innerHTML = html;
    const el = actionMsgEl.firstElementChild;
    if (!el) return;
    actionMsgEl._clearTimer = setTimeout(() => {
        if (!actionMsgEl.contains(el)) return;
        el.style.transition = 'opacity 0.5s';
        el.style.opacity = '0';
        setTimeout(() => {
            if (el.parentNode === actionMsgEl) actionMsgEl.innerHTML = '';
        }, 500);
    }, 10000);
}

function startShortPoll() {
    let active = true;

    function tick() {
        if (!active) return;
        fetch('/status')
            .then(r => r.json())
            .then(d => {
                if (!active) return;
                if (d.status === 'synced') {
                    active = false;
                    done();
                } else if (d.status === 'failed') {
                    active = false;
                    $('postSubmitMsg').classList.add('hidden');
                    $('wifiForm').classList.remove('hidden');
                    subBtn.disabled = false;
                    subBtn.innerText = 'Savienot';
                    statusEl.innerHTML = '<p class="error">Savienojums neizdevās. Pārbaudiet paroli un mēģiniet vēlreiz.</p>';
                } else if (d.status === 'connected') {
                    pollStatusEl.innerHTML = '<p class="success">Sinhronizē laiku...</p>';
                }
                if (active) setTimeout(tick, 1000);
            })
            .catch(() => {
                if (active) setTimeout(tick, 1000);
            });
    }

    tick();
}

async function loadSaved() {
    try {
        const r = await fetch('/saved');
        savedNetworks = await r.json();
    } catch (_) {
        savedNetworks = [];
    }
}

function resetForm(isSaved) {
    pwField.classList.toggle('hidden', isSaved);
    pwHead.classList.toggle('hidden', isSaved);
    editBtn.classList.toggle('hidden', !isSaved);
    if (!isSaved) pwField.value = '';
    if (isSaved) editBtn.textContent = 'Mainīt paroli';
    subBtn.textContent = 'Savienot';
}

async function deleteSaved(ssid) {
    try {
        const r = await fetch('/delete', {
            method: 'POST',
            headers: {'Content-Type': 'application/json'},
            body: JSON.stringify({ssid})
        });
        if (!r.ok) {
            showTempStatus('<p class="error">Neizdevās dzēst tīklu!</p>');
            return;
        }
        savedNetworks = savedNetworks.filter(n => n !== ssid);
        markSavedInScan();
        document.querySelectorAll('.saved-unavailable').forEach(el => {
            const span = el.querySelector('span');
            if (span && !getSaved(span.textContent)) el.remove();
        });
        if (!$('ssidList').querySelector('.saved-unavailable')) {
            const heading = $('ssidList').querySelector('.saved-section-heading');
            if (heading) heading.remove();
        }
        const sel = document.querySelector('input[name="s"]:checked');
        if (sel && sel.value === ssid) resetForm(false);
        showTempStatus('<p class="success">Tīkls "' + esc(ssid) + '" dzēsts!</p>');
    } catch (_) {
        showTempStatus('<p class="error">Kļūda: neizdevās sazināties ar ierīci!</p>');
    }
}

function markSavedInScan() {
    document.querySelectorAll('.wifi-item').forEach(item => {
        const span = item.querySelector('span');
        if (!span) return;
        const ssid = span.textContent;
        const saved = getSaved(ssid);
        let badge = item.querySelector('.saved-badge');
        let del = item.querySelector('.del-btn');
        if (saved) {
            if (!badge) {
                badge = document.createElement('span');
                badge.className = 'saved-badge';
                badge.textContent = '\u2713';
                item.appendChild(badge);
            }
            if (!del) {
                del = document.createElement('button');
                del.type = 'button';
                del.className = 'del-btn';
                del.textContent = '\u2715';
                del.onclick = function (e) {
                    e.stopPropagation();
                    deleteSaved(ssid).catch(() => {
                    });
                };
                item.appendChild(del);
            }
        } else {
            if (badge) badge.remove();
            if (del) del.remove();
        }
    });
}

function onNetworkSelect(ssid) {
    resetForm(!!getSaved(ssid));
}

function editPassword() {
    const sel = document.querySelector('input[name="s"]:checked');
    if (!sel) return;
    if (pwField.classList.contains('hidden')) {
        pwField.classList.remove('hidden');
        pwHead.classList.remove('hidden');
        pwField.value = '';
        editBtn.textContent = 'Atcelt';
    } else {
        pwField.classList.add('hidden');
        pwHead.classList.add('hidden');
        pwField.value = '';
        editBtn.textContent = 'Mainīt paroli';
    }
}

async function scanNetworks() {
    try {
        const r = await fetch('/scan');
        const networks = await r.json();
        const list = $('ssidList');
        const loading = $('loading');
        loading.classList.add('hidden');
        list.innerHTML = '';
        if (networks.length === 0) {
            list.innerHTML = '<p class="error">Nav atrasts neviens tīkls!</p>';
        } else {
            networks.forEach(ssid => {
                const label = document.createElement('label');
                label.className = 'wifi-item';
                const safe = esc(ssid);
                label.innerHTML = '<input type="radio" name="s" value="' + safe + '" required><span>' + safe + '</span>';
                list.appendChild(label);
            });
            document.querySelectorAll('input[name="s"]').forEach(r => r.addEventListener('change', function () {
                onNetworkSelect(this.value);
            }));
        }
        const unavailable = savedNetworks.filter(n => !networks.includes(n));
        if (unavailable.length > 0) {
            const heading = document.createElement('div');
            heading.className = 'saved-section-heading';
            heading.textContent = 'Saglabātie tīkli';
            list.appendChild(heading);
            unavailable.forEach(ssid => {
                const el = document.createElement('div');
                el.className = 'wifi-item saved-unavailable';
                el.innerHTML = '<span>' + esc(ssid) + '</span>';
                list.appendChild(el);
            });
        }
        markSavedInScan();
        $('scanHint').classList.remove('hidden');
    } catch (_) {
        $('loading').innerHTML = '<span class="error">Kļūda meklējot!</span>';
        $('scanHint').classList.remove('hidden');
    }
}

function done() {
    $('setupContainer').classList.add('hidden');
    $('doneContainer').classList.remove('hidden');
    setTimeout(() => location.reload(), 60000);
}

async function submit(e) {
    e.preventDefault();
    const sel = document.querySelector('input[name="s"]:checked');
    if (!sel) return;
    const ssid = sel.value;
    const isSaved = getSaved(ssid);
    const editingPassword = !pwField.classList.contains('hidden');
    subBtn.disabled = true;
    statusEl.innerHTML = '<p class="loading">Saglabā datus...</p>';
    try {
        let r;
        if (isSaved && !editingPassword) {
            subBtn.innerText = 'Savienojas...';
            r = await fetch('/reconnect', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({ssid})
            });
        } else {
            subBtn.innerText = 'Saglabā...';
            r = await fetch('/save', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({ssid, password: pwField.value})
            });
        }
        const d = await r.json();
        if (!d.success) {
            statusEl.innerHTML = '<p class="error">' + esc(errorMessages[d.error] || errorMessages._unknown) + '</p>';
            subBtn.disabled = false;
            subBtn.innerText = 'Savienot';
            return;
        }
        if (!isSaved) savedNetworks.push(ssid);
        $('wifiForm').classList.add('hidden');
        $('postSubmitMsg').classList.remove('hidden');
        pollStatusEl.innerHTML = '<p class="loading">Savienojas...</p>';
        startShortPoll();
    } catch (err) {
        statusEl.innerHTML = '<p class="error">' + errorMessages._network + '</p>';
        subBtn.disabled = false;
        subBtn.innerText = 'Savienot';
    }
}

async function loadApiSettings() {
    try {
        const r = await fetch('/api-settings');
        const d = await r.json();
        if (d.api_url) $('apiUrl').value = d.api_url;
        if (d.api_key) $('apiKey').value = d.api_key;
    } catch (_) {}
}

async function saveApiSettings() {
    const url = $('apiUrl').value.trim();
    const key = $('apiKey').value.trim();
    if (!url && !key)
    {
        showTempStatus('<p class="error">Ievadiet API URL vai atslēgu!</p>');
        return;
    }
    const today = new Date().toISOString().slice(0, 10);
    try {
        const r = await fetch('/api-settings', {
            method: 'POST',
            headers: {'Content-Type': 'application/json'},
            body: JSON.stringify({api_url: url, api_key: key, test_date: today})
        });
        const d = await r.json();
        if (d.success) {
            showTempStatus('<p class="success">Iestatījumi saglabāti!</p>');
        } else if (d.error) {
            showTempStatus('<p class="error">' + esc(d.error) + '</p>');
        }
    } catch (_) {
        showTempStatus('<p class="error">Kļūda saglabājot iestatījumus!</p>');
    }
}

async function clearApiSettings() {
    $('apiUrl').value = '';
    $('apiKey').value = '';
    try {
        const r = await fetch('/api-settings', {
            method: 'POST',
            headers: {'Content-Type': 'application/json'},
            body: JSON.stringify({api_url: '', api_key: '', test_date: ''})
        });
        const d = await r.json();
        if (d.success) {
            showTempStatus('<p class="success">Iestatījumi dzēsti!</p>');
        } else if (d.error) {
            showTempStatus('<p class="error">' + esc(d.error) + '</p>');
        }
    } catch (_) {
        showTempStatus('<p class="error">Kļūda dzēšot iestatījumus!</p>');
    }
}

function init() {
    pwField = $('password');
    pwHead = $('passwordHeading');
    editBtn = $('editBtn');
    subBtn = $('submitBtn');
    statusEl = $('statusMsg');
    actionMsgEl = $('actionMsg');
    pollStatusEl = $('pollStatus');
    editBtn.onclick = editPassword;
    $('doneContainer').classList.add('hidden');
    $('rescanBtn').onclick = async function () {
        this.disabled = true;
        this.textContent = 'Meklē...';
        try {
            await fetch('/rescan', {method: 'POST'});
            await scanNetworks();
        } catch (_) {
            showTempStatus('<p class="error">Kļūda pārmeklējot tīklus!</p>');
        }
        this.disabled = false;
        this.textContent = 'Pārmeklēt no jauna';
    };
    $('wifiForm').onsubmit = submit;
    $('saveApiUrlBtn').onclick = saveApiSettings;
    $('deleteApiUrlBtn').onclick = clearApiSettings;
}

function start() {
    loadSaved().then(() => scanNetworks()).catch(() => {
    });
    loadApiSettings();
    fetch('/status').then(r => r.json()).then(d => {
        if (d.status === 'failed') {
            statusEl.innerHTML = '<p class="error">Iepriekšējais savienojums neizdevās. Mainiet paroli vai izvēlieties citu tīklu.</p>';
        } else if (d.status !== '' && d.status !== 'failed') {
            $('wifiForm').classList.add('hidden');
            $('postSubmitMsg').classList.remove('hidden');
            if (d.status === 'synced') {
                done();
            } else {
                pollStatusEl.innerHTML = '<p class="loading">Savienojas...</p>';
                startShortPoll();
            }
        }
    }).catch(() => {
    });
}

init();
start();
