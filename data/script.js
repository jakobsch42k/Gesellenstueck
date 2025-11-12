
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