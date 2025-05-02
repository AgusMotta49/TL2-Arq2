// Configuración Firebase
import { firebaseConfig } from './firebase-config.js';

// Inicializar Firebase
firebase.initializeApp(firebaseConfig);
const database = firebase.database();

// Objetos para gráficos
let chartsRealTime = {
    tempDHT: null,
    humDHT: null,
    tempBMP: null,
    presBMP: null
};

let chartsHistorial = {
    tempDHT: null,
    humDHT: null,
    tempBMP: null,
    presBMP: null
};

// ================== TIEMPO REAL ==================

function inicializarGraficosTiempoReal() {
    const commonConfig = {
        type: 'line',
        options: {
            responsive: true,
            maintainAspectRatio: true,
            aspectRatio: 2,
            interaction: {
                mode: 'nearest',
                intersect: false
            },
            plugins: {
                tooltip: {
                    enabled: true,
                    mode: 'index',
                    callbacks: {
                        label: (context) => `${context.dataset.label}: ${context.parsed.y}`
                    }
                }
            },
            scales: {
                x: { 
                    display: false,
                },
                y: {
                    beginAtZero: false,
                    title: { display: true, text: 'Valor' }
                }
            }
        }
    };

    chartsRealTime.tempDHT = new Chart(document.getElementById('chartTempDHTReal'), {
        ...commonConfig,
        data: { labels: [], datasets: [{ label: 'Temp DHT22 (°C)', borderColor: 'red', data: [] }] }
    });

    chartsRealTime.humDHT = new Chart(document.getElementById('chartHumDHTReal'), {
        ...commonConfig,
        data: { labels: [], datasets: [{ label: 'Humedad DHT22 (%)', borderColor: 'blue', data: [] }] }
    });

    chartsRealTime.tempBMP = new Chart(document.getElementById('chartTempBMPReal'), {
        ...commonConfig,
        data: { labels: [], datasets: [{ label: 'Temp BMP280 (°C)', borderColor: 'orange', data: [] }] }
    });

    chartsRealTime.presBMP = new Chart(document.getElementById('chartPresBMPReal'), {
        ...commonConfig,
        data: { labels: [], datasets: [{ label: 'Presión BMP280 (hPa)', borderColor: 'green', data: [] }] }
    });

    precargarDatosTiempoReal();
}

function precargarDatosTiempoReal() {
    database.ref('lecturas').limitToLast(5).once('value').then(snapshot => {
        if (!snapshot.exists()) {
            mostrarMensajeSinDatos();
            return;
        }

        ocultarMensajeSinDatos();

        snapshot.forEach(childSnapshot => {
            const datos = childSnapshot.val();
            const hora = childSnapshot.key.split('-').slice(3, 6).join(':');

            chartsRealTime.tempDHT.data.labels.push(hora);
            chartsRealTime.tempDHT.data.datasets[0].data.push(datos.tempDHT);

            chartsRealTime.humDHT.data.labels.push(hora);
            chartsRealTime.humDHT.data.datasets[0].data.push(datos.humDHT);

            chartsRealTime.tempBMP.data.labels.push(hora);
            chartsRealTime.tempBMP.data.datasets[0].data.push(datos.tempBMP);

            chartsRealTime.presBMP.data.labels.push(hora);
            chartsRealTime.presBMP.data.datasets[0].data.push(datos.presionBMP);
        });

        Object.values(chartsRealTime).forEach(chart => chart.update());

        escucharDatosTiempoReal();
    });
}

function escucharDatosTiempoReal() {
    database.ref('lecturas').limitToLast(1).on('child_added', (snapshot) => {
        const datos = snapshot.val();
        const hora = snapshot.key.split('-').slice(3, 6).join(':');

        ocultarMensajeSinDatos();

        Object.entries(chartsRealTime).forEach(([key, chart]) => {
            if (chart.data.labels.includes(hora)) return; // evitar duplicados

            if (chart.data.labels.length >= 20) {
                chart.data.labels.shift();
                chart.data.datasets[0].data.shift();
            }

            chart.data.labels.push(hora);
        });

        chartsRealTime.tempDHT.data.datasets[0].data.push(datos.tempDHT);
        chartsRealTime.humDHT.data.datasets[0].data.push(datos.humDHT);
        chartsRealTime.tempBMP.data.datasets[0].data.push(datos.tempBMP);
        chartsRealTime.presBMP.data.datasets[0].data.push(datos.presionBMP);

        Object.values(chartsRealTime).forEach(chart => chart.update());
    });
}

function mostrarMensajeSinDatos() {
    const msg = document.getElementById('sinDatosTiempoReal');
    if (msg) msg.style.display = 'block';
}

function ocultarMensajeSinDatos() {
    const msg = document.getElementById('sinDatosTiempoReal');
    if (msg) msg.style.display = 'none';
}

// ================== HISTORIAL ==================
async function cargarHistorial() {
    const fechaInicio = document.getElementById('fechaInicio').value + "-00-00-00";
    const fechaFin = document.getElementById('fechaFin').value + "-23-59-59";

    const snapshot = await database.ref("lecturas")
        .orderByKey()
        .startAt(fechaInicio)
        .endAt(fechaFin)
        .once("value");

    const datos = snapshot.val();
    if (!datos) {
        alert("No hay datos en este rango");
        return;
    }

    // Destruir gráficos antiguos
    Object.values(chartsHistorial).forEach(chart => chart && chart.destroy());

    // Crear nuevos gráficos
    const timestamps = Object.keys(datos);
    const fechasConHora = timestamps.map(ts => ts.replace(/-/g, ':').replace(/^(\d+):(\d+):(\d+):/, '$1-$2-$3 '));
    const fechasSoloDia = timestamps.map(ts => ts.split('-').slice(0, 3).join('-'));
//    const labels = timestamps.map(ts => ts.split('-').slice(0, 3).join('-')); // Fecha sin hora

    chartsHistorial.tempDHT = crearGraficoHistorial(
        'chartTempDHTHistorial',
        'Temp DHT22 (°C)',
        fechasSoloDia,
        timestamps.map(ts => datos[ts].tempDHT),
        'red',
        fechasConHora
    );

    chartsHistorial.humDHT = crearGraficoHistorial(
        'chartHumDHTHistorial',
        'Humedad DHT22 (%)',
        fechasSoloDia,
        timestamps.map(ts => datos[ts].humDHT),
        'blue',
        fechasConHora
    );

    chartsHistorial.tempBMP = crearGraficoHistorial(
        'chartTempBMPHistorial',
        'Temp BMP280 (°C)',
        fechasSoloDia,
        timestamps.map(ts => datos[ts].tempBMP),
        'orange',
        fechasConHora
    );

    chartsHistorial.presBMP = crearGraficoHistorial(
        'chartPresBMPHistorial',
        'Presión BMP280 (hPa)',
        fechasSoloDia,
        timestamps.map(ts => datos[ts].presionBMP),
        'green',
        fechasConHora
    );
}

// ================== HISTORIAL ==================
function crearGraficoHistorial(id, label, labels, data, color, labelsTooltip) {
    return new Chart(document.getElementById(id), {
        type: 'line',
        data: {
            labels: labels,
            datasets: [{
                label: label,
                data: data,
                borderColor: color,
                fill: false,
                tension: 0.1
            }]
        },
        options: {
            responsive: true,
            maintainAspectRatio: true,
            aspectRatio: 2,
            interaction: {
                mode: 'nearest',
                intersect: false
            },
            plugins: {
                tooltip: {
                    callbacks: {
                        title: (context) => `Fecha y hora: ${labelsTooltip[context[0].dataIndex]}`,
                        label: (context) => `${context.dataset.label}: ${context.parsed.y} ${obtenerUnidad(context.dataset.label)}`
                    }
                }
            },
            scales: {
                x: {
                    display: true,
                    title: { display: true, text: 'Fecha' },
                    ticks: {
                        autoSkip: true,
                        maxRotation: 45,
                        minRotation: 45
                    }
                },
                y: {
                    beginAtZero: false,
                    title: { display: true, text: 'Valor' }
                }
            }
        }
    });
}

function obtenerUnidad(label) {
    if (label.includes('Temp')) return '°C';
    if (label.includes('Humedad')) return '%';
    if (label.includes('Presión')) return 'hPa';
    return '';
}


// ================== NAVEGACIÓN ==================
document.getElementById('btnTiempoReal').addEventListener('click', () => {
    document.getElementById('seccionTiempoReal').classList.add('seccion-activa');
    document.getElementById('seccionHistorial').classList.remove('seccion-activa');
    document.getElementById('btnTiempoReal').classList.add('active');
    document.getElementById('btnHistorial').classList.remove('active');
});

document.getElementById('btnHistorial').addEventListener('click', () => {
    document.getElementById('seccionHistorial').classList.add('seccion-activa');
    document.getElementById('seccionTiempoReal').classList.remove('seccion-activa');
    document.getElementById('btnHistorial').classList.add('active');
    document.getElementById('btnTiempoReal').classList.remove('active');
});

// Inicialización
inicializarGraficosTiempoReal();

