/*
 * ESP32 Plant Bed Control - JavaScript Inte    // --- Konfiguration laden ---
    // Function to load configuration from the ESP32 server
    function loadConfig() {
      // Fetch configuration data from /loadConfig endpoint
      fetch('/loadConfig')
        .then(r => r.json())  // Parse JSON response containing configuration
        .then(cfg => {
          // Update each bed's moisture target slider and display value
          for (let i = 1; i <= 5; i++) {
            const val = cfg[`moisture${i}`];  // Get moisture value for bed i
            document.getElementById(`beet${i}Target`).value = val;  // Set slider value
            document.getElementById(`beet${i}TargetVal`).innerText = val;  // Update display text
          }
          // Update light target input field
          document.getElementById('lightTarget').value = cfg.lux;
        })
        .then(msg => console.log('✅ Konfiguration geladen'))  // Log success message
        .catch(err => console.error('Fehler beim Laden der Config:', err));  // Log any errors
    }his script handles the client-side functionality for the ESP32 plant bed
 * control web interface. It manages:
 *
 * - Tab navigation between different sections (dashboard, settings, system)
 * - Real-time sensor data updates from the ESP32 server
 * - Configuration loading and saving (moisture targets, light settings)
 * - Plant database management (adding plants, assigning to beds)
 * - Dynamic UI updates (slider values, dropdowns)
 *
 * The script uses fetch API for HTTP requests to the ESP32 web server
 * and manipulates the DOM to update the user interface dynamically.
 *
 * Key functions:
 * - showSection(): Tab switching functionality
 * - loadConfig()/saveConfig(): Configuration management
 * - loadPlants()/addPlant(): Plant database operations
 * - Real-time data updates via setInterval
 */

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

    // --- Live-Daten regelmäßig abrufen ---
    // Set up periodic fetching of sensor data from the ESP32 server
    setInterval(() => {
      // Fetch sensor data from /data.json endpoint
      fetch('/data.json')
        .then(r => r.json())  // Parse JSON response
        .then(data => {
          // Update DOM elements with current sensor readings
          document.getElementById('light').innerText = data.light + ' lx';        // Light level in lux
          document.getElementById('temp').innerText = data.temperature + ' °C';   // Temperature in Celsius
          document.getElementById('humidity').innerText = data.humidity + ' %';  // Humidity percentage
          document.getElementById('pumpStatus').innerText = data.pumpStatus;     // Pump operational status
        })
        .catch(err => console.error('Fehler beim Datenabruf:', err));  // Log any fetch errors
    }, 2000);  // Update every 2 seconds// --- Konfiguration laden ---
// Function to load configuration from the ESP32 server
function loadConfig() {
  // Fetch configuration data from /loadConfig endpoint
  fetch('/loadConfig')
    .then(r => r.json())  // Parse JSON response containing configuration
    .then(cfg => {
      // Update each bed's moisture target slider and display value
      for (let i = 1; i <= 5; i++) {
        const val = cfg[`moisture${i}`];  // Get moisture value for bed i
        document.getElementById(`beet${i}Target`).value = val;  // Set slider value
        document.getElementById(`beet${i}TargetVal`).innerText = val;  // Update display text
      }
      // Update light target input field
      document.getElementById('lightTarget').value = cfg.lux;
    })
    .then(msg => console.log('✅ Konfiguration geladen'))  // Log success message
    .catch(err => console.error('Fehler beim Laden der Config:', err));  // Log any errors
}

    // --- Konfiguration speichern ---
    // Function to save current configuration to the ESP32 server
    function saveConfig() {
      // Build configuration object from current UI values
      const cfg = {};
      for (let i = 1; i <= 5; i++) {
        // Get moisture target value from each bed's slider
        cfg[`moisture${i}`] = parseInt(document.getElementById(`beet${i}Target`).value);
      }
      // Get light target value
      cfg.lux = parseInt(document.getElementById('lightTarget').value);

      // Send configuration to server via POST request
      fetch('/saveConfig', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },  // Specify JSON content type
        body: JSON.stringify(cfg)  // Convert configuration object to JSON string
      })
        .then(res => res.text())  // Get response text
        .then(msg => alert('✅ Konfiguration gespeichert!\n' + msg))  // Show success message
        .catch(err => alert('❌ Fehler beim Speichern: ' + err));  // Show error message
    }// --- Slideranzeige aktualisieren ---
// Set up event listeners when DOM is fully loaded
window.addEventListener('DOMContentLoaded', () => {
  // Get all slider elements
  const sliders = document.querySelectorAll('.slider');
  // Add input event listener to each slider for real-time value updates
  sliders.forEach(slider => {
    slider.addEventListener('input', () => {
      // Update the corresponding display element with current slider value
      document.getElementById(slider.id + 'Val').innerText = slider.value;
    });
  });

  loadConfig(); // Load current configuration from server on page load
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
    sel.innerHTML = '<option value="">-- auswählen --</option>';  // Reset with default option
    plants.forEach(p => {  // Add each plant as an option
      const opt = document.createElement('option');
      opt.value = p.name;
      opt.textContent = p.name;
      sel.appendChild(opt);
    });
  }

  // Also update the delete plant dropdown
  const deleteSel = document.getElementById('deletePlantSelect');
  deleteSel.innerHTML = '<option value="">-- Pflanze zum Löschen auswählen --</option>';
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
  if (!name || isNaN(moisture)) return alert('Bitte Name und Ziel-Feuchte angeben');

  // Send new plant data to server
  fetch('/addPlant', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({name: name, targetMoisture: moisture})
  })
  .then(res => {
    if (res.status === 409) {  // Plant name already exists (HTTP 409 Conflict)
      throw new Error('Eine Pflanze mit diesem Namen existiert bereits');
    } else if (!res.ok) {  // Other server errors
      throw new Error('Server-Fehler: ' + res.status);
    }
    return res.text();  // Get response text for success
  })
  .then(msg => {
    alert('✅ Pflanze gespeichert!');
    // Clear input fields
    document.getElementById('newPlantName').value = '';
    document.getElementById('newPlantMoisture').value = '';
    loadPlants(); // Reload plant database to update dropdowns
  })
  .catch(err => alert('❌ Fehler: ' + err.message));  // Show specific error message
}

// Function to delete a plant from the database
function deletePlant() {
  // Get selected plant name from dropdown
  const plantName = document.getElementById('deletePlantSelect').value;
  if (!plantName) {
    alert('Bitte wählen Sie eine Pflanze zum Löschen aus');
    return;
  }

  // Confirm deletion with user
  if (!confirm(`Sind Sie sicher, dass Sie die Pflanze "${plantName}" löschen möchten?`)) {
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
      throw new Error('Pflanze nicht gefunden');
    } else if (!res.ok) {  // Other server errors
      throw new Error('Server-Fehler: ' + res.status);
    }
    return res.text();  // Get response text for success
  })
  .then(msg => {
    alert('✅ Pflanze gelöscht!');
    // Clear the delete dropdown selection
    document.getElementById('deletePlantSelect').value = '';
    loadPlants(); // Reload plant database to update all dropdowns
  })
  .catch(err => alert('❌ Fehler: ' + err.message));
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

// Initialize plant management when DOM is fully loaded
window.addEventListener('DOMContentLoaded', () => {
  loadPlants();  // Load plant database and populate dropdowns
});