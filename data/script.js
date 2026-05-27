// Tab navigation function - switches between different sections of the interface
function showSection(id) {
  // Remove 'active' class from all navigation buttons
  document.querySelectorAll('nav button').forEach(btn => btn.classList.remove('active'));
  // Hide all sections by removing 'active' class
  document.querySelectorAll('section').forEach(sec => sec.classList.remove('active'));
  // Add 'active' class to the clicked navigation button
  document.querySelector(`button[onclick="showSection('${id}')"]`).classList.add('active');
  // Show the selected section by adding 'active' class
  document.getElementById(id).classList.add('active');
}

    // --- Fetch live data regularly ---
    // Set up periodic fetching of sensor data from the ESP32 server
    setInterval(() => {
      // Fetch sensor data from /data.json endpoint
      fetch('/data.json')
        .then(r => r.json())  // Parse JSON response
        .then(data => {
          // Update DOM elements with current sensor readings
          document.getElementById('temp').firstChild.textContent     = data.tempC;
          document.getElementById('humidity').firstChild.textContent = data.humPerc;
          document.getElementById('light').firstChild.textContent    = data.lux;
          document.getElementById('pumpStatus').innerText            = data.pumpStatus;
          updateMaintenanceUI(!!data.MAINTENANCE_MODE);

          if (data.soilPerc) {
            for (let i = 1; i <= 5; i++) {
              const moisture = data.soilPerc[i - 1];
              document.getElementById(`bed${i}_moisture`).innerText = moisture;
              const fill = document.getElementById(`bed${i}_visual`);
              fill.style.width = moisture + '%';
              fill.classList.toggle('low', moisture < 30);
            }
          }
        })
        .catch(err => console.error('Error fetching data:', err));

      // Fetch system status data from /systemStatus endpoint
      fetch('/systemStatus')
        .then(r => r.json())  // Parse JSON response
        .then(data => {
          // Update DOM elements with system status information
          document.getElementById('wifiStatus').innerText = data.wifi;           // WiFi connection status
          document.getElementById('uptime').innerText = data.uptime;             // Server uptime
        })
        .catch(err => console.error('Error fetching system status:', err));

      // Fetch diagnostics data from /diagnostics endpoint
      fetch('/diagnostics')
        .then(r => r.json())  // Parse JSON response
        .then(data => {
          // Update sensor raw values
          document.getElementById('diag_moisture1').innerText = data.soilRaw[0];
          document.getElementById('diag_moisture2').innerText = data.soilRaw[1];
          document.getElementById('diag_moisture3').innerText = data.soilRaw[2];
          document.getElementById('diag_moisture4').innerText = data.soilRaw[3];
          document.getElementById('diag_moisture5').innerText = data.soilRaw[4];
          document.getElementById('diag_temperature').firstChild.textContent = data.temperature;
          document.getElementById('diag_humidity').firstChild.textContent    = data.humidity;
          document.getElementById('diag_light').firstChild.textContent       = data.light;
          document.getElementById('diag_water_low').innerText      = data.waterLow;
          document.getElementById('diag_water_critical').innerText = data.waterCritical;
          document.getElementById('diag_roof_contact').innerText   = data.roofContact;
          
          // Update system states
          document.getElementById('diag_pump_status').innerText = data.pumpStatus;
          document.getElementById('diag_error_flags').innerText = data.errorFlags;
          document.getElementById('diag_roof_state').innerText = data.roofState;
          document.getElementById('roof_viz').dataset.state = data.roofState;
          currentRoofState = data.roofState;

          // Update system information
          document.getElementById('diag_comm_status').innerText = data.commStatus;
          document.getElementById('diag_led_pwm').firstChild.textContent = data.ledPWM;
          document.getElementById('diag_free_heap').firstChild.textContent = (data.freeHeap / 1024).toFixed(1);
        })
        .catch(err => console.error('Error fetching diagnostics:', err));
    }, 2000);  // Update every 2 seconds// --- Konfiguration laden ---
function loadConfig() {
  fetch('/loadConfig')
    .then(r => r.json())
    .then(cfg => {
      // moisture is an array in the new backend format
      for (let i = 1; i <= 5; i++) {
        const val = cfg.moisture ? cfg.moisture[i - 1] : cfg[`moisture${i}`];
        document.getElementById(`beet${i}Target`).value = val;
        document.getElementById(`beet${i}TargetVal`).innerText = val;
      }
      // luxTarget in new backend, lux in old
      document.getElementById('lightTarget').value = cfg.luxTarget || cfg.lux || 500;

      // populate hourly light profile
      if (cfg.lightProfile && cfg.lightProfile.length === 24) {
        for (let h = 0; h < 24; h++) {
          const input = document.getElementById(`lightProfile_${h}`);
          const bar   = document.getElementById(`lightProfileBar_${h}`);
          if (input) input.value = cfg.lightProfile[h];
          if (bar)   bar.style.height = cfg.lightProfile[h] + '%';
        }
      }
    })
    .then(() =>console.log('Configuration loaded'))
    .catch(err => console.error('Error loading config:', err));
}

    // --- Konfiguration speichern ---
    function saveConfig() {
      const cfg = {};

      // moisture as array (new backend format)
      cfg.moisture = [];
      for (let i = 1; i <= 5; i++) {
        cfg.moisture.push(parseInt(document.getElementById(`beet${i}Target`).value) || 0);
      }

      cfg.luxTarget = parseInt(document.getElementById('lightTarget').value) || 500;

      // collect hourly light profile
      cfg.lightProfile = [];
      for (let h = 0; h < 24; h++) {
        const el = document.getElementById(`lightProfile_${h}`);
        cfg.lightProfile.push(el ? Math.min(100, Math.max(0, parseInt(el.value) || 0)) : 0);
      }

      fetch('/saveConfig', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },  // Specify JSON content type
        body: JSON.stringify(cfg)  // Convert configuration object to JSON string
      })
        .then(res => res.text())  // Get response text
        .then(() =>alert('Configuration saved.'))
        .catch(err => alert('Error saving: ' + err));
    }// --- Light profile grid builder ---
function buildLightProfileGrid() {
  const grid = document.getElementById('lightProfileGrid');
  if (!grid) return;

  const currentHour = new Date().getHours();

  for (let h = 0; h < 24; h++) {
    const col = document.createElement('div');
    col.className = 'lp-col' + (h === currentHour ? ' active-hour' : '');

    col.innerHTML =
      '<div class="lp-bar-track">' +
        '<div class="lp-bar" id="lightProfileBar_' + h + '"></div>' +
      '</div>' +
      '<input type="number" class="lp-input" id="lightProfile_' + h + '" ' +
             'min="0" max="100" value="50">' +
      '<div class="lp-label">' + String(h + 1).padStart(2, '0') + '</div>';

    grid.appendChild(col);

    const input = col.querySelector('input');
    const track = col.querySelector('.lp-bar-track');
    const bar   = col.querySelector('.lp-bar');

    function setVal(pct) {
      pct = Math.min(100, Math.max(0, Math.round(pct)));
      input.value = pct;
      bar.style.height = pct + '%';
    }

    input.addEventListener('input', function() {
      setVal(parseInt(this.value) || 0);
    });

    let dragging = false;

    track.addEventListener('pointerdown', function(e) {
      dragging = true;
      track.setPointerCapture(e.pointerId);
      const rect = track.getBoundingClientRect();
      setVal(100 - ((e.clientY - rect.top) / rect.height) * 100);
    });

    track.addEventListener('pointermove', function(e) {
      if (!dragging) return;
      const rect = track.getBoundingClientRect();
      setVal(100 - ((e.clientY - rect.top) / rect.height) * 100);
    });

    track.addEventListener('pointerup',     function() { dragging = false; });
    track.addEventListener('pointercancel', function() { dragging = false; });
  }
}

// --- Slideranzeige aktualisieren ---
window.addEventListener('DOMContentLoaded', () => {
  const sliders = document.querySelectorAll('.slider');
  sliders.forEach(slider => {
    slider.addEventListener('input', () => {
      document.getElementById(slider.id + 'Val').innerText = slider.value;
    });
  });

  buildLightProfileGrid();
  loadConfig();
  autoSetTime();
});

// --- Pflanzenverwaltung ---
// Global array to store plant database
let plants = [];

// Function to load plant database from server
function loadPlants() {
  // Fetch plant data from /getPlants endpoint
  fetch('/getPlants')
    .then(r => r.json())  // Parse JSON response
    .then(data => {
      plants = data;  // Store plant data globally
      updatePlantDropdowns();  // Update all plant selection dropdowns
    });
}

// Function to update all plant selection dropdowns with current plant database
function updatePlantDropdowns() {
  for (let i = 1; i <= 5; i++) {  // Loop through all 5 beds
    const sel = document.getElementById(`beet${i}Plant`);  // Get dropdown element
    sel.innerHTML = '<option value="">-- select --</option>';  // Reset with default option
    plants.forEach(p => {  // Add each plant as an option
      const opt = document.createElement('option');
      opt.value = p.name;
      opt.textContent = p.name;
      sel.appendChild(opt);
    });
  }

  // Also update the delete plant dropdown
  const deleteSel = document.getElementById('deletePlantSelect');
  deleteSel.innerHTML = '<option value="">-- Select plant to delete --</option>';
  plants.forEach(p => {
    const opt = document.createElement('option');
    opt.value = p.name;
    opt.textContent = p.name;
    deleteSel.appendChild(opt);
  });
}

// Function to add a new plant to the database
function addPlant() {
  // Get input values from form fields
  const name = document.getElementById('newPlantName').value.trim();
  const moisture = parseInt(document.getElementById('newPlantMoisture').value);
  // Validate inputs
  if (!name || isNaN(moisture)) return alert('Please enter name and target moisture');

  // Send new plant data to server
  fetch('/addPlant', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({name: name, targetMoisture: moisture})
  })
  .then(res => {
    if (res.status === 409) {  // Plant name already exists (HTTP 409 Conflict)
      throw new Error('A plant with this name already exists');
    } else if (!res.ok) {  // Other server errors
      throw new Error('Server error: ' + res.status);
    }
    return res.text();  // Get response text for success
  })
  .then(() =>{
    alert('Plant saved!');
    // Clear input fields
    document.getElementById('newPlantName').value = '';
    document.getElementById('newPlantMoisture').value = '';
    loadPlants(); // Reload plant database to update dropdowns
  })
  .catch(err => alert('Error: ' + err.message));  // Show specific error message
}

// Function to delete a plant from the database
function deletePlant() {
  // Get selected plant name from dropdown
  const plantName = document.getElementById('deletePlantSelect').value;
  if (!plantName) {
    alert('Please select a plant to delete');
    return;
  }

  // Confirm deletion with user
  if (!confirm(`Are you sure you want to delete the plant "${plantName}"?`)) {
    return;
  }

  // Send delete request to server
  fetch('/deletePlant', {
    method: 'DELETE',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({name: plantName})
  })
  .then(res => {
    if (res.status === 404) {  // Plant not found
      throw new Error('Plant not found');
    } else if (!res.ok) {  // Other server errors
      throw new Error('Server error: ' + res.status);
    }
    return res.text();  // Get response text for success
  })
  .then(() =>{
    alert('Plant deleted!');
    // Clear the delete dropdown selection
    document.getElementById('deletePlantSelect').value = '';
    loadPlants(); // Reload plant database to update all dropdowns
  })
  .catch(err => alert('Error: ' + err.message));
}

// Function to automatically set moisture target when a plant is selected from dropdown
function setMoistureFromPlant(beetIndex) {
  // Get the selected plant dropdown element
  const sel = document.getElementById(`beet${beetIndex}Plant`);
  // Find the selected plant in the plants array
  const plant = plants.find(p => p.name === sel.value);

  if (plant) {
    // Get target moisture from plant data
    let targetMoisture = plant.targetMoisture;
    // Ensure moisture value is within valid range (0-100%)
    targetMoisture = Math.min(100, Math.max(0, targetMoisture));

    // Update the slider and display value
    document.getElementById(`beet${beetIndex}Target`).value = targetMoisture;
    document.getElementById(`beet${beetIndex}TargetVal`).innerText = targetMoisture;
  }
}

// --- Manual Control Functions ---
// Formats a Date object as "YYYY-MM-DDTHH:mm" for datetime-local inputs
function toLocalDatetimeString(date) {
  const pad = n => String(n).padStart(2, '0');
  return date.getFullYear() + '-' + pad(date.getMonth() + 1) + '-' + pad(date.getDate()) +
         'T' + pad(date.getHours()) + ':' + pad(date.getMinutes());
}

// Silently syncs browser time to ESP32 on page load
function autoSetTime() {
  const now = new Date();
  const input = document.getElementById('timeInput');
  if (input) input.value = toLocalDatetimeString(now);

  // Send local seconds-since-midnight so ESP32 hour matches the highlighted bar
  const unixTimestamp = now.getHours() * 3600 + now.getMinutes() * 60 + now.getSeconds();
  fetch('/setTime', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ timestamp: unixTimestamp })
  })
  .then(res => {
    if (!res.ok) throw new Error(res.status);
    document.getElementById('systemTime').innerText = now.toLocaleString();
    document.getElementById('timeStatus').innerText = 'Auto-synced on load';
    setTimeout(() => { document.getElementById('timeStatus').innerText = ''; }, 3000);
  })
  .catch(err => console.warn('[time] auto-sync failed:', err));
}

// Function to set system time via HTTP
function setSysTime() {
  const timeInput = document.getElementById('timeInput').value;
  if (!timeInput) {
    alert('Please select a date and time');
    return;
  }

  // Parse the datetime-local format (YYYY-MM-DDTHH:mm) — getHours() returns local time
  const dateObj = new Date(timeInput);
  const unixTimestamp = dateObj.getHours() * 3600 + dateObj.getMinutes() * 60 + dateObj.getSeconds();

  // Send time to server
  fetch('/setTime', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ timestamp: unixTimestamp })
  })
  .then(res => {
    if (!res.ok) throw new Error(`Error: ${res.status}`);
    return res.text();
  })
  .then(() =>{
    document.getElementById('timeStatus').innerText = 'Time set successfully';
    document.getElementById('timeStatus').style.color = 'var(--accent)';
    setTimeout(() => {
      document.getElementById('timeStatus').innerText = '';
    }, 3000);
  })
  .catch(err => {
    console.error(`Error: ${err.message}`);
    document.getElementById('timeStatus').innerText = `Error: ${err.message}`;
    document.getElementById('timeStatus').style.color = 'var(--red)';
  });
}

// Function to control pump (on/off)
function controlPump(state) {
  const command = state ? 'pump_on' : 'pump_off';
  sendManualCommand(command, 'pump');
}

// Function to control solenoid valves (open/close)
function controlValve(valve, state) {
  const command = state ? 'valve_open' : 'valve_close';
  sendManualCommand(command, `valve${valve}`, valve);
}

// Latest roof state reported by /diagnostics (updated every poll)
let currentRoofState = null;

// Function to control roof motor (open/close/stop)
function controlRoofMotor(direction) {
  if (direction === 'open' && (currentRoofState === 'OPEN' || currentRoofState === 'OPENING')) {
    alert('Roof already open');
    return;
  }
  sendManualCommand(`roof_${direction}`, 'roof');
}

// Function to set LED brightness (PWM value)
function setLEDBrightness() {
  const pwmValue = parseInt(document.getElementById('ledPWM').value);
  sendManualCommand(`led_pwm`, 'led', pwmValue);
}

// Function to toggle maintenance mode on/off
function toggleMaintenance() {
  const active = document.getElementById('maintenanceBtn').dataset.active === 'true';
  const cmd = active ? 'maintenance_off' : 'maintenance_on';
  fetch('/manual', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ command: cmd })
  })
  .then(res => { if (!res.ok) throw new Error(res.status); })
  .then(() => updateMaintenanceUI(!active))
  .catch(err => alert('Error: ' + err));
}

function updateMaintenanceUI(active) {
  const btn    = document.getElementById('maintenanceBtn');
  const status = document.getElementById('maintenanceStatus');
  const banner = document.getElementById('maintenanceBanner');
  if (!btn) return;
  btn.dataset.active = active;
  const mc = document.getElementById('manualControls');
  if (active) {
    btn.textContent    = 'Deactivate';
    status.innerHTML   = '<strong>Maintenance Mode:</strong> ON — automatic regulation paused.';
    banner.style.borderLeftColor = 'var(--accent)';
    banner.style.background      = 'rgba(76, 175, 120, 0.08)';
    banner.style.color           = 'var(--accent)';
    if (mc) mc.style.display = '';
  } else {
    btn.textContent    = 'Activate';
    status.innerHTML   = '<strong>Maintenance Mode:</strong> Off — automatic regulation is running.';
    banner.style.borderLeftColor = '';
    banner.style.background      = '';
    banner.style.color           = '';
    if (mc) mc.style.display = 'none';
  }
}

// Function to reset emergency stop and all latching error flags
function ackErrors() {
  fetch('/ackErrors', { method: 'POST', headers: { 'Content-Type': 'application/json' } })
    .then(res => { if (!res.ok) throw new Error(res.status); return res.text(); })
    .then(() => { document.getElementById('emergency_last').innerText = 'Never (reset)'; })
    .catch(err => alert('Reset failed: ' + err));
}

// Function to trigger emergency stop
function emergencyStop() {
  sendManualCommand('emergency_stop', 'emergency');
  document.getElementById('emergency_last').innerText = new Date().toLocaleTimeString();
  fetch('/manual', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ command: 'maintenance_on' })
  }).then(() => updateMaintenanceUI(true));
}

// Generic function to send manual control commands
function sendManualCommand(command, component, value = null) {
  const payload = { command: command };
  if (value !== null) payload.value = value;

  fetch('/manual', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(payload)
  })
  .then(res => {
    if (!res.ok) throw new Error(`Error: ${res.status}`);
    return res.text();
  })
  .then(() =>{
    console.log(`${command} executed`);
    // Show brief feedback
    const statusId = `manual_${component}_status`;
    if (document.getElementById(statusId)) {
      document.getElementById(statusId).innerText = 'OK';
      setTimeout(() => {
        document.getElementById(statusId).innerText = '--';
      }, 1000);
    }
  })
  .catch(err => {
    console.error(`Error: ${err.message}`);
    alert(`Error controlling ${component}: ${err.message}`);
  });
}

// Update LED PWM display value when slider moves
window.addEventListener('DOMContentLoaded', () => {
  const ledSlider = document.getElementById('ledPWM');
  if (ledSlider) {
    ledSlider.addEventListener('input', () => {
      const pwmValue = parseInt(ledSlider.value);
      document.getElementById('led_pwm_value').innerText = pwmValue;
    });
  }
  loadPlants();  // Load plant database and populate dropdowns
});