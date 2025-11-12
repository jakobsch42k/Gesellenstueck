const char htmlPage[] = R"rawliteral(
  <!DOCTYPE html>
  <html lang="de">
  <head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Hochbeet Steuerung – ESP32</title>
  <style>
    body {  
      font-family: "Segoe UI", Arial, sans-serif;
      background-color: #f3f6fa;
      margin: 0;
      color: #333;
    }
    header {
      background-color: #2c3e50;
      color: white;
      padding: 15px;
      text-align: center;
    }
    nav {
      display: flex;
      justify-content: center;
      background: #34495e;
    }
    nav button {
      background: none;
      border: none;
      color: white;
      padding: 14px 20px;
      cursor: pointer;
      font-size: 16px;
    }
    nav button:hover, nav button.active {
      background-color: #1abc9c;
    }
    section {
      display: none;
      padding: 20px;
    }
    section.active {
      display: block;
    }
    .sensor-grid {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(220px, 1fr));
      gap: 20px;
    }
    .card {
      background: white;
      border-radius: 12px;
      box-shadow: 0 3px 6px rgba(0,0,0,0.1);
      padding: 15px;
      text-align: center;
    }
    .card h3 {
      margin-top: 0;
      color: #1abc9c;
    }
    .bar-container {
      width: 100%;
      background: #e0e0e0;
      border-radius: 10px;
      overflow: hidden;
      height: 16px;
    }
    .bar {
      height: 100%;
      background-color: #1abc9c;
      width: 0%;
      transition: width 0.5s ease-in-out;
    }
    .slider {
      width: 100%;
    }
    footer {
      text-align: center;
      font-size: 13px;
      padding: 10px;
      color: #777;
    }
  </style>
  </head>
  <body>
  <header>
    <h1>🌱 Hochbeet-Steuerung ESP32</h1>
  </header>

  <nav>
    <button class="active" onclick="showSection('dashboard')">Dashboard</button>
    <button onclick="showSection('beete')">Beeteinstellungen</button>
    <button onclick="showSection('system')">Systemstatus</button>
  </nav>

  <!-- DASHBOARD -->
  <section id="dashboard" class="active">
    <h2>Live-Daten</h2>
    <div class="sensor-grid">
      <div class="card">
        <h3>Temperatur</h3>
        <p><strong id="temp">{{temp}} °C</strong></p>
      </div>
      <div class="card">
        <h3>Luftfeuchtigkeit</h3>
        <p><strong id="humidity">{{humidity}} %</strong></p>
      </div>
      <div class="card">
        <h3>Helligkeit (BH1750)</h3>
        <p><strong id="light">{{light}} lx</strong></p>
      </div>
      <div class="card">
        <h3>Pumpenstatus</h3>
        <p><strong id="pumpStatus">{{pumpStatus}}</strong></p>
      </div>
    </div>
    <h3 style="margin-top:30px;">Bodenfeuchte der Beete</h3>
    <div class="sensor-grid">
      <div class="card">
        <h3>Beet 1</h3>
        <div class="bar-container"><div id="beet1Bar" class="bar" style="width: {{beet1}}%;"></div></div>
        <p id="beet1Val">{{beet1}} %</p>
      </div>
      <div class="card">
        <h3>Beet 2</h3>
        <div class="bar-container"><div id="beet2Bar" class="bar" style="width: {{beet2}}%;"></div></div>
        <p id="beet2Val">{{beet2}} %</p>
      </div>
      <div class="card">
        <h3>Beet 3</h3>
        <div class="bar-container"><div id="beet3Bar" class="bar" style="width: {beet3}}%"></div></div>
        <p id="beet3Val">{{beet3}} %</p>
      </div>
      <div class="card">
        <h3>Beet 4</h3>
        <div class="bar-container"><div id="beet4Bar" class="bar" style="width: {{beet4}}%;"></div></div>
        <p id="beet4Val">{{beet4}} %</p>
      </div>
      <div class="card">
        <h3>Beet 5</h3>
        <div class="bar-container"><div id="beet5Bar" class="bar" style="width: {{beet5}}%;"></div></div>
        <p id="beet5Val">{{beet5}} %</p>
      </div>
    </div>

    <h3 style="margin-top:30px;">Wassertank</h3>
    <div class="sensor-grid">
      <div class="card">
        <h3>Oberer Schwimmschalter</h3>
        <p><strong id="floatHigh">{{floatHigh}}</strong></p>
      </div>
      <div class="card">
        <h3>Unterer Schwimmschalter</h3>
        <p><strong id="floatLow">{{floatLow}}</strong></p>
      </div>
    </div>

    <h3 style="margin-top:30px;">Dachstatus</h3>
    <div class="sensor-grid">
      <div class="card">
        <h3>Dach</h3>
        <p><strong id="roofStatus">{{roofStatus}}</strong></p>
      </div>
    </div>
  </section>

  <!-- BEETEINSTELLUNGEN -->
  <section id="beete">
    <h2>Beeteinstellungen</h2>
    <div class="sensor-grid">
      <div class="card">
        <h3>Beet 1 Ziel-Feuchte</h3>
        <input type="range" min="0" max="100" value="{{beet1Target}}" class="slider" id="beet1Target">
        <p><span id="beet1TargetVal">{{beet1Target}}</span> %</p>
      </div>
      <div class="card">
        <h3>Beet 2 Ziel-Feuchte</h3>
        <input type="range" min="0" max="100" value="{{beet2Target}}" class="slider" id="beet2Target">
        <p><span id="beet2TargetVal">{{beet2Target}}</span> %</p>
      </div>
      <div class="card">
        <h3>Beet 3 Ziel-Feuchte</h3>
        <input type="range" min="0" max="100" value="{{beet3Target}}" class="slider" id="beet3Target">
        <p><span id="beet3TargetVal">{{beet3Target}}</span> %</p>
      </div>
      <div class="card">
        <h3>Beet 4 Ziel-Feuchte</h3>
        <input type="range" min="0" max="100" value="{{beet4Target}}" class="slider" id="beet4Target">
        <p><span id="beet4TargetVal">{{beet4Target}}</span> %</p>
      </div>
      <div class="card">
        <h3>Beet 5 Ziel-Feuchte</h3>
        <input type="range" min="0" max="100" value="{{beet5Target}}" class="slider" id="beet5Target">
        <p><span id="beet5TargetVal">{{beet5Target}}</span> %</p>
      </div>
    </div>

    <h3 style="margin-top:30px;">Lichtsteuerung</h3>
    <div class="card">
      <label>Benötigte Helligkeit (Lux):</label>
      <input type="number" id="lightTarget" value="{{lightTarget}}" min="0" max="10000">
    </div>
  </section>

  <!-- SYSTEMSTATUS -->
  <section id="system">
    <h2>Systemstatus</h2>
    <div class="sensor-grid">
      <div class="card">
        <h3>Verbindungsstatus</h3>
        <p id="wifiStatus">{{wifiStatus}}</p>
      </div>
      <div class="card">
        <h3>Letzte Aktualisierung</h3>
        <p id="lastUpdate">{{lastUpdate}}</p>
      </div>
      <div class="card">
        <h3>ESP Uptime</h3>
        <p id="uptime">{{uptime}}</p>
      </div>
    </div>
  </section>

  <footer>
    © 2025 Hochbeet Steuerung – ESP32 | WebUI Version 1.0
  </footer>

  <script>
    // Navigation
    function showSection(id) {
      document.querySelectorAll('nav button').forEach(btn => btn.classList.remove('active'));
      document.querySelectorAll('section').forEach(sec => sec.classList.remove('active'));
      document.querySelector(`button[onclick="showSection('${id}')"]`).classList.add('active');
      document.getElementById(id).classList.add('active');
    }

    // Sliderwerte anzeigen
    const sliders = document.querySelectorAll('.slider');
    sliders.forEach(slider => {
      slider.addEventListener('input', () => {
        document.getElementById(slider.id + 'Val').innerText = slider.value;
      });
    });

    // Automatische Aktualisierung vorbereiten
    setInterval(() => {
      fetch('/data')
        .then(r => r.json())
        .then(data => {
          document.getElementById('light').innerText = data.light + ' lx';
          document.getElementById('temp').innerText = data.temperature + ' °C';
          document.getElementById('humidity').innerText = data.humidity + ' %';
        });
    }, 1000);
  </script>
  </body>
  </html>

)rawliteral";