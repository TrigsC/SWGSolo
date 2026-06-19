const state = {
  token: localStorage.getItem("core3_api_token") || "",
  refreshMs: 5000,
  timer: null
};

const $ = (id) => document.getElementById(id);

const number = (value) => new Intl.NumberFormat().format(Number(value || 0));

const labelize = (value) => String(value || "none")
  .replace(/_/g, " ")
  .replace(/\b\w/g, (char) => char.toUpperCase());

function setServerState(message, tone = "muted") {
  const node = $("server-state");
  node.textContent = message;
  node.dataset.tone = tone;
}

function renderBars(activity) {
  const rows = [
    ["Queued", activity.queued],
    ["Moving", activity.moving],
    ["Sampling", activity.sampling],
    ["Validated", activity.validated],
    ["Failed", activity.failed]
  ];
  const max = Math.max(1, ...rows.map(([, value]) => Number(value || 0)));

  $("miner-bars").innerHTML = rows.map(([label, value]) => {
    const width = Math.max(3, Math.round((Number(value || 0) / max) * 100));
    return `
      <div class="bar-row">
        <span>${label}</span>
        <div class="bar-track"><div class="bar-fill" style="width:${width}%"></div></div>
        <strong>${number(value)}</strong>
      </div>
    `;
  }).join("");
}

function renderSupplyList(id, rows, maxRows = 8) {
  const node = $(id);
  const sorted = [...(rows || [])].sort((a, b) => Number(b.quantity || 0) - Number(a.quantity || 0));

  if (sorted.length === 0) {
    node.innerHTML = `<div class="empty">No supply recorded</div>`;
    return;
  }

  node.innerHTML = sorted.slice(0, maxRows).map((row) => `
    <div class="supply-row">
      <span title="${row.label}">${labelize(row.label)}</span>
      <strong>${number(row.quantity)}</strong>
    </div>
  `).join("");
}

function renderDemand(demand) {
  const rows = demand.profiles || [];
  $("demand-state").textContent = demand.activeResourceSnapshotAvailable
    ? `${rows.length} profiles`
    : `snapshot unavailable`;

  $("demand-table").innerHTML = rows.map((row) => {
    const opportunity = row.activeOpportunityResource || {};
    const opportunityText = opportunity.available
      ? `<div class="opportunity">${opportunity.resourceName}<small>${opportunity.resourceType} &middot; ${opportunity.zones || "unknown zone"}</small></div>`
      : `<div class="opportunity"><small>${labelize(opportunity.reason || "none")}</small></div>`;

    return `
      <tr>
        <td>${labelize(row.profile)}</td>
        <td><span class="chip ${row.stateGroup}">${labelize(row.stateGroup)}</span></td>
        <td>${number(row.desiredReserve)}</td>
        <td>${number(row.knownSupply)}</td>
        <td>${number(row.pressureScore)}</td>
        <td>${opportunityText}</td>
      </tr>
    `;
  }).join("") || `<tr><td colspan="6"><div class="empty">No demand profiles enabled</div></td></tr>`;
}

function renderSafety(safety) {
  const rows = [
    ["Real resource creation", safety.realResourceCreation],
    ["ResourceContainer creation", safety.resourceContainerCreation],
    ["Market mutation", safety.marketMutation],
    ["Inventory mutation", safety.inventoryMutation]
  ];

  $("safety-list").innerHTML = rows.map(([label, value]) => `
    <div class="safety-row">
      <span>${label}</span>
      <strong>${String(value || "no").toUpperCase()}</strong>
    </div>
  `).join("");
}

function renderAssignments(assignments) {
  const node = $("assignment-list");
  const rows = [...(assignments || [])].sort((a, b) => Number(a.remainingSeconds || 0) - Number(b.remainingSeconds || 0));

  if (rows.length === 0) {
    node.innerHTML = `<div class="empty">No live assignments</div>`;
    return;
  }

  node.innerHTML = rows.slice(0, 10).map((row) => `
    <div class="assignment-row">
      <span title="${row.targetResource || "none"}">${labelize(row.status)} - ${labelize(row.profile)}</span>
      <strong>${number(row.remainingSeconds)}s</strong>
    </div>
  `).join("");
}

function renderSnapshot(snapshot) {
  const population = snapshot.population || {};
  const activity = snapshot.minerActivity || {};
  const supply = snapshot.supply || {};

  $("as-of").textContent = snapshot.metadata?.asOfTime || "live";
  $("active-miners").textContent = number(population.activeMiners);
  $("active-pvp").textContent = number(population.activePvpBots);
  $("pvp-status").textContent = population.pvpStatus || "experimental";

  $("miner-mode").textContent = `mode ${activity.mode || "off"}`;
  $("intelligent-active").textContent = number(activity.currentIntelligentActiveCount);
  $("activation-failures").textContent = number(activity.activationFailures);
  $("path-failures").textContent = number(activity.pathFailures);
  $("max-active").textContent = number(activity.maxActiveIntelligentMiners);
  $("cooldown").textContent = `${number(activity.cooldownSeconds)}s`;

  const emergency = $("emergency-state");
  emergency.textContent = activity.emergencyDisabled ? "Emergency disabled" : "Emergency clear";
  emergency.classList.toggle("danger", Boolean(activity.emergencyDisabled));
  renderBars(activity);

  $("session-total").textContent = number(supply.currentSessionConceptualTotalQuantity);
  $("persistent-total").textContent = number(supply.persistentBaselineStockpileQuantity);
  $("known-total").textContent = number(supply.totalKnownConceptualQuantity);
  $("supply-status").textContent = `${supply.persistentBaselineStatus || "disabled"} - ${number(supply.persistentBaselineStockpileLots)} lots`;
  renderSupplyList("session-list", supply.currentSessionConceptualTotals);
  renderSupplyList("known-list", supply.totalKnownConceptualSupply);

  renderDemand(snapshot.demand || {});
  renderSafety(snapshot.safetyBoundaries || {});
  renderAssignments(activity.assignments || []);
}

async function loadSnapshot() {
  if (!state.token) {
    setServerState("Awaiting local API token");
    return;
  }

  try {
    const response = await fetch("/v1/aieconomy/dashboard/", {
      headers: {
        Authorization: `Bearer ${state.token}`
      },
      cache: "no-store"
    });

    if (!response.ok) {
      throw new Error(`${response.status} ${response.statusText}`);
    }

    const payload = await response.json();
    renderSnapshot(payload.result || payload);
    setServerState("Connected to Core3 REST API", "ok");
  } catch (error) {
    setServerState(`Connection failed: ${error.message}`, "error");
  }
}

function schedule() {
  window.clearInterval(state.timer);
  state.timer = window.setInterval(loadSnapshot, state.refreshMs);
}

$("token-input").value = state.token;
$("token-form").addEventListener("submit", (event) => {
  event.preventDefault();
  state.token = $("token-input").value.trim();

  if (state.token) {
    localStorage.setItem("core3_api_token", state.token);
  } else {
    localStorage.removeItem("core3_api_token");
  }

  loadSnapshot();
  schedule();
});

loadSnapshot();
schedule();
