 function showSection(id) {
      document.querySelectorAll('nav button').forEach(btn => btn.classList.remove('active'));
      document.querySelectorAll('section').forEach(sec => sec.classList.remove('active'));
      document.querySelector(`button[onclick="showSection('${id}')"]`).classList.add('active');
      document.getElementById(id).classList.add('active');
    }

    // --- Live-Daten regelmäßig abrufen ---
    setInterval(() => {
      fetch('/data.json')
        .then(r => r.json())
        .then(data => {
          document.getElementById('light').innerText = data.light + ' lx';
          document.getElementById('temp').innerText = data.temperature + ' °C';
          document.getElementById('humidity').innerText = data.humidity + ' %';
          document.getElementById('pumpStatus').innerText = data.pumpStatus;
        })
        .catch(err => console.error('Fehler beim Datenabruf:', err));
    }, 2000);

    // --- Konfiguration laden ---
function loadConfig() {
  fetch('/loadConfig')
    .then(r => r.json())
    .then(cfg => {
      for (let i = 1; i <= 5; i++) {
        const val = cfg[`moisture${i}`];
        document.getElementById(`beet${i}Target`).value = val;
        document.getElementById(`beet${i}TargetVal`).innerText = val;
      }
      document.getElementById('lightTarget').value = cfg.lux;
    })
    .then(msg => console.log('✅ Konfiguration geladen'))
    .catch(err => console.error('Fehler beim Laden der Config:', err));
}

    // --- Konfiguration speichern ---
      function saveConfig() {
    const cfg = {};
    for (let i = 1; i <= 5; i++) {
      cfg[`moisture${i}`] = parseInt(document.getElementById(`beet${i}Target`).value);
    }
    cfg.lux = parseInt(document.getElementById('lightTarget').value);

    fetch('/saveConfig', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(cfg)
    })
      .then(res => res.text())
      .then(msg => alert('✅ Konfiguration gespeichert!\n' + msg))
      .catch(err => alert('❌ Fehler beim Speichern: ' + err));
  }

    // --- Slideranzeige aktualisieren ---
    window.addEventListener('DOMContentLoaded', () => {
      const sliders = document.querySelectorAll('.slider');
      sliders.forEach(slider => {
        slider.addEventListener('input', () => {
          document.getElementById(slider.id + 'Val').innerText = slider.value;
        });
      });

      loadConfig(); // beim Laden direkt aktuelle Konfig holen
    });