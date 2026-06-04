/* ===================================================== */
/* WEBSOCKET */
/* ===================================================== */

const ws =
new WebSocket(`ws://${window.location.hostname}:81`);

let myClientId = null;

let currentController = 255;

const MAX_POINTS = 20;

let voltageData = [];
let currentData = [];
let fpData = [];

let labels = [];

const voltageChart = new Chart(
document.getElementById("voltageChart"),
{
    type: 'line',
    data: {
        labels: labels,
        datasets: [{
            label: 'Voltaje',
            data: voltageData,
            borderColor: '#22c55e',
            tension: 0.3,
            pointRadius:0,
            borderWidth:2,
        }]
    },
    options:{
      responsive:true,
      maintainAspectRatio:false,
      animation:false,
      scales:{
          x:{
              display:false
          },
          y:{
              min:100,
              max:140,
              ticks:{
                  stepSize:10
              }
          }
      }
  }
});

const currentChart = new Chart(
document.getElementById("currentChart"),
{
    type:'line',
    data:{
        labels:labels,
        datasets:[{
            label:'Corriente',
            data:currentData,
            borderColor:'#3b82f6',
            tension:0.3,
            pointRadius:0,
            borderWidth:2,
        }]
    },
    options:{
      responsive:true,
      maintainAspectRatio:false,
      animation:false,
      scales:{
          x:{
              display:false
          },
          y:{
              min:0,
              max:1,
              ticks:{
                  stepSize:0.1
              }
          }
      }
  }
});

const fpChart = new Chart(
document.getElementById("fpChart"),
{
    type:'line',
    data:{
        labels:labels,
        datasets:[{
            label:'FP',
            data:fpData,
            borderColor:'#f59e0b',
            tension:0.3,
            pointRadius:0,
            borderWidth:2,
        }]
    },
    options:{
      responsive:true,
      maintainAspectRatio:false,
      animation:false,
      scales:{
          x:{
              display:false
          },
          y:{
              min:0,
              max:1,
              ticks:{
                  stepSize:0.1
              }
          }
      }
  }
});

function updateCharts(voltage,current,fp){

    const timestamp =
    new Date().toLocaleTimeString();

    labels.push(timestamp);

    voltageData.push(voltage);
    currentData.push(current);
    fpData.push(fp);

    if(labels.length > MAX_POINTS){

        labels.shift();

        voltageData.shift();
        currentData.shift();
        fpData.shift();

    }

    voltageChart.update('none');
    currentChart.update('none');
    fpChart.update('none');

}

/* ===================================================== */
/* ENVIAR */
/* ===================================================== */

function sendCommand(cmd){

  if(ws.readyState === WebSocket.OPEN){

    ws.send(cmd);

  }

}

/* ===================================================== */
/* WEBSOCKET OPEN */
/* ===================================================== */

ws.onopen = () => {

  console.log("WEBSOCKET CONNECTED");

};

/* ===================================================== */
/* MENSAJES */
/* ===================================================== */

ws.onmessage = (event) => {

  const msg = event.data;

  /* ========================= */
  /* TELEMETRIA */
  /* ========================= */

  if(msg.includes(",")){

    const values = msg.split(",");

    if(values.length == 7){

      document.getElementById("rssi-value")
      .innerText =
      parseFloat(values[0]).toFixed(2) + " dBm";

      document.getElementById("voltage-value")
      .innerText =
      parseFloat(values[1]).toFixed(2) + " V";

      document.getElementById("current-value")
      .innerText =
      parseFloat(values[2]).toFixed(2) + " A";

      document.getElementById("apparent-power-value")
      .innerText =
      parseFloat(values[3]).toFixed(2) + " VA";

      document.getElementById("power-value")
      .innerText =
      parseFloat(values[4]).toFixed(2) + " W";

      document.getElementById("reactive-power-value")
      .innerText =
      parseFloat(values[5]).toFixed(2) + " VAR";

      document.getElementById("fp-value")
      .innerText =
      parseFloat(values[6]).toFixed(2);

      updateCharts(
          parseFloat(values[1]),
          parseFloat(values[2]),
          parseFloat(values[6])
      );

    }

    return;

  }

  /* ========================= */
  /* RELAY ON */
  /* ========================= */

  if(msg == "RELAY_ON"){

    const indicator =
    document.getElementById("relay-indicator");

    indicator.innerText = "ON";

    indicator.classList.remove("relay-off");

    indicator.classList.add("relay-on");

    return;

  }

  /* ========================= */
  /* RELAY OFF */
  /* ========================= */

  if(msg == "RELAY_OFF"){

    const indicator =
    document.getElementById("relay-indicator");

    indicator.innerText = "OFF";

    indicator.classList.remove("relay-on");

    indicator.classList.add("relay-off");

    return;

  }

  /* ========================= */
  /* LECTURA ON */
  /* ========================= */

  if(msg == "READING_ON"){

    const indicator =
    document.getElementById("reading-indicator");

    indicator.innerText = "ON";

    indicator.classList.remove("relay-off");

    indicator.classList.add("relay-on");

    return;

  }

  /* ========================= */
  /* LECTURA OFF */
  /* ========================= */

  if(msg == "READING_OFF"){

    const indicator =
    document.getElementById("reading-indicator");

    indicator.innerText = "OFF";

    indicator.classList.remove("relay-on");

    indicator.classList.add("relay-off");

    return;

  }

  /* ========================= */
  /* ID */
  /* ========================= */

  if(msg.startsWith("ASSIGN_ID_")){

    myClientId =
    parseInt(msg.split("_")[2]);

    return;

  }

  /* ========================= */
  /* CONTROL */
  /* ========================= */

  if(msg.startsWith("CTRL_")){

    currentController =
    parseInt(msg.split("_")[1]);

    updateControlButtons();

    return;

  }

};

/* ===================================================== */
/* CONTROL BOTONES */
/* ===================================================== */

function updateControlButtons(){

  const acceptBtn =
  document.getElementById("accept-control");

  const releaseBtn =
  document.getElementById("release-control");

  if(currentController == myClientId){

    acceptBtn.disabled = true;
    releaseBtn.disabled = false;

    acceptBtn.style.opacity = "0.5";
    releaseBtn.style.opacity = "1";

  }

  else if(currentController == 255){

    acceptBtn.disabled = false;
    releaseBtn.disabled = true;

    acceptBtn.style.opacity = "1";
    releaseBtn.style.opacity = "0.5";

  }

  else{

    acceptBtn.disabled = true;
    releaseBtn.disabled = true;

    acceptBtn.style.opacity = "0.5";
    releaseBtn.style.opacity = "0.5";

  }

}

/* ===================================================== */
/* BOTONES */
/* ===================================================== */

document.getElementById("accept-control")
.addEventListener("click", () => {

  sendCommand("REQUEST_CONTROL");

});

document.getElementById("release-control")
.addEventListener("click", () => {

  sendCommand("RELEASE_CONTROL");

});

document.getElementById("btn-on")
.addEventListener("click", () => {

  if(currentController == myClientId){

    sendCommand("ON");

  }

});

document.getElementById("btn-off")
.addEventListener("click", () => {

  if(currentController == myClientId){

    sendCommand("OFF");

  }

});

document.getElementById("btn-go")
.addEventListener("click", () => {

  if(currentController == myClientId){

    sendCommand("GO");

  }

});

document.getElementById("btn-stop")
.addEventListener("click", () => {

  if(currentController == myClientId){

    sendCommand("STOP");

  }

});

updateControlButtons();

/* ===================================================== */
/* BOTONES LORA */
/* ===================================================== */

document.getElementById("btn-lora-short")
.addEventListener("click", () => {

    sendCommand("LORA_SHORT");

});

document.getElementById("btn-lora-mid")
.addEventListener("click", () => {

    sendCommand("LORA_MID");

});

document.getElementById("btn-lora-long")
.addEventListener("click", () => {

    sendCommand("LORA_LONG");

});

/* ===================================================== */
/* LOAD LIST */
/* ===================================================== */

const loadSelect =
document.getElementById("load-select");

loadSelect.addEventListener("change", () => {

    const selected =
    loadSelect.value;

    const currentLoad =
    document.getElementById("current-load");

    const estimatedPower =
    document.getElementById("estimated-power");

    const description =
    document.getElementById("load-description");

    /* ========================= */
    /* FOCO */
    /* ========================= */

    if(selected === "foco"){

        currentLoad.innerText =
        "Foco";

        estimatedPower.innerText =
        "72 W";

        description.innerText =
        "Foco incandescente de uso doméstico (resistivo)";

    }

    /* ========================= */
    /* VENTILADOR */
    /* ========================= */

    else if(selected === "ventilador"){

        currentLoad.innerText =
        "Ventilador";

        estimatedPower.innerText =
        "21 W";

        description.innerText =
        "Ventilador pequeño doméstico para uso personal";

    }

    /* ========================= */
    /* NINGUNO */
    /* ========================= */

    else{

        currentLoad.innerText =
        "Ninguna";

        estimatedPower.innerText =
        "--- W";

        description.innerText =
        "Selecciona una carga de la lista.";

    }

});
