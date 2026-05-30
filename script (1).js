/* ===================================================== */
/* WEBSOCKET */
/* ===================================================== */

const ws =
new WebSocket(`ws://${window.location.hostname}:81`);

let myClientId = null;

let currentController = 255;

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