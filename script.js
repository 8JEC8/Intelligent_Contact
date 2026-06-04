/* ===================================================== */
/* GENERAL */
/* ===================================================== */

*{
  margin:0;
  padding:0;
  box-sizing:border-box;
  font-family:Arial, Helvetica, sans-serif;
}

body{
  background:#0f172a;
  color:white;
  min-height:100vh;
  display:flex;
  justify-content:center;
  align-items:center;
  padding:30px;
}

.container{
  width:1400px;
  min-height:1250px;
  background:#111827;
  border-radius:20px;
  padding:25px;
  border:2px solid #334155;
  box-shadow:0px 0px 25px rgba(0,0,0,0.5);
}

/* ===================================================== */
/* HEADER */
/* ===================================================== */

.header{
  width:100%;
  height:70px;
  background:#1e293b;
  border-radius:15px;
  display:flex;
  align-items:center;
  justify-content:center;
  margin-bottom:20px;
  border:1px solid #334155;
}

.header h1{
  font-size:32px;
  letter-spacing:2px;
}

/* ===================================================== */
/* GRID */
/* ===================================================== */

.main-grid{
  display:grid;
  grid-template-columns:1fr 1fr;
  gap:20px;
  align-items:start;
}

.left-column,
.right-column{
  display:flex;
  flex-direction:column;
  gap:20px;
}

.panel{
  background:#1e293b;
  border-radius:18px;
  border:1px solid #334155;
  padding:20px;
}

/* ===================================================== */
/* DATOS */
/* ===================================================== */

.data-panel h2{
  margin-bottom:20px;
}

.data-box{
  background:#0f172a;
  border-radius:15px;
  padding:18px;
  margin-bottom:15px;
  border:1px solid #334155;
  display:flex;
  justify-content:space-between;
  align-items:center;
}

.data-label{
  font-size:20px;
}

.data-value{
  font-size:24px;
  color:#22c55e;
  font-weight:bold;
}

.rssi-value{
  color:#f59e0b;
}

/* ===================================================== */
/* GRAFICAS */
/* ===================================================== */

.graph-panel h2{
  margin-bottom:20px;
}

.graphs{
  display:grid;
  grid-template-columns:repeat(3,1fr);
  gap:12px;
}

.graph-box{
  background:#0f172a;
  border-radius:15px;
  border:1px solid #334155;
  padding:15px;

  display:flex;
  flex-direction:column;

  min-height:180px;
}

.graph-title{
  font-size:18px;
  color:#e2e8f0;
  text-align:center;
  margin-bottom:10px;
}

.graph-box canvas{
  width:100% !important;
  height:150px !important;
}

.graph-title{
  font-size:22px;
  color:#e2e8f0;
}

/* ===================================================== */
/* ZONA INFERIOR */
/* ===================================================== */

.bottom-left{
  display:grid;
  grid-template-columns:390px 240px 1fr;
  gap:20px;
  align-items:start;
}

.status-panel{
  width:100%;
}

.status-panel h2{
  margin-bottom:17px;
}


/* ===================================================== */
/* CONTROL */
/* ===================================================== */

.control-panel{
  flex:1;
}

.control-panel h2{
  margin-bottom:20px;
}

.button-grid{
  display:grid;
  grid-template-columns:repeat(2,1fr);
  gap:20px;
  margin-top:25px;
}

button{
  height:90px;
  border:none;
  border-radius:15px;
  font-size:24px;
  font-weight:bold;
  cursor:pointer;
  transition:0.25s;
  color:white;
}

button:hover{
  transform:scale(1.05);
}

.btn-on{
  background:#16a34a;
}

.btn-off{
  background:#dc2626;
}

.btn-go{
  background:#2563eb;
}

.btn-stop{
  background:#e7a303;
  color:black;
}

.btn-control-accept{
  background:#16a34a;
}

.btn-control-release{
  background:#dc2626;
}

/* ===================================================== */
/* ESTADOS */
/* ===================================================== */

.status-panel{
  width:260px;
  height: 300px;
  background:#1e293b;
  border-radius:18px;
  border:1px solid #334155;
  padding:20px;
}

.status-title{
  font-size:26px;
  margin-bottom:20px;
}

.status-label{
  font-size:22px;
  margin-bottom:10px;
  margin-top:20px;
}

.relay-indicator{
  width:100%;
  height:60px;
  border-radius:15px;
  display:flex;
  justify-content:center;
  align-items:center;
  font-size:30px;
  font-weight:bold;
  color:white;
  background:#dc2626;
}

.relay-on{
  background:#16a34a;
}

.relay-off{
  background:#dc2626;
}

.relay-box h3{
  margin-bottom:12px;
  font-size:24px;
}

/* ===================================================== */
/* LOAD LIST */
/* ===================================================== */

.load-list{
  grid-column:1 / span 2;
}

.load-list h2{
  margin-bottom:20px;
  align-self:start;
}

.load-list-container{
  display:flex;
  flex-direction:column;
  gap:20px;
}

select{
  width:100%;
  height:45px;
  border-radius:12px;
  border:none;
  padding:10px;
  font-size:15px;
  background:#0f172a;
  color:white;
  border:1px solid #334155;
}

.load-info{
  background:#0f172a;
  border-radius:15px;
  border:1px solid #334155;
  padding:20px;
  color:#cbd5e1;
  line-height:1.8;
  font-size:17px;
  min-height:280px;
}

/* ===================================================== */
/* RESPONSIVE */
/* ===================================================== */

@media(max-width:1200px){

  .main-grid{
    grid-template-columns:1fr;
  }

  .load-list{
    grid-column:auto;
  }

  .bottom-left{
    flex-direction:column;
  }

  .status-panel{
    width:100%;
  }

}

@media(max-width:768px){

  body{
    padding:10px;
  }

  .container{
    width:100%;
    padding:15px;
  }

  .graphs{
    grid-template-columns:1fr;
  }

  button{
    height:70px;
    font-size:20px;
  }

}

/* ===================================================== */
/* LORA */
/* ===================================================== */

.lora-panel{
  margin-top:20px;
}

.lora-panel h2{
  margin-bottom:20px;
}

.lora-buttons{
  display:grid;
  grid-template-columns:repeat(3,1fr);
  gap:20px;
}

.btn-lora-short{
  background:#585858;
}

.btn-lora-mid{
  background:#585858;
}

.btn-lora-long{
  background:#585858;
}

.btn-lora-short,
.btn-lora-mid,
.btn-lora-long{

  height:80px;
  font-size:22px;   

}
