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
    ["Stationed", activity.stationed],
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

function renderEconomyDecisionAudit(audit = {}) {
  const counts = audit.counts || {};
  const blockers = audit.blockers || {};
  const profiles = [...(audit.profileAudit || [])];
  const status = audit.status || "no_data";
  const safetyClear = !audit.realResourceCreated &&
    !audit.resourceContainerCreated &&
    !audit.inventoryMutated &&
    !audit.economyMutated &&
    !audit.marketMutated;

  $("economy-audit-state").textContent = `${audit.mode || "read-only"} · ${labelize(status)}`;
  const statusNode = $("economy-audit-status");
  statusNode.textContent = labelize(status).toUpperCase();
  statusNode.className = `chip ${status}`;
  $("economy-audit-recommendation").textContent = labelize(audit.recommendation || "do_not_change_behavior_yet");
  $("economy-audit-summary").textContent = audit.summary || "No audit data available yet.";

  const safetyNode = $("economy-audit-safety");
  safetyNode.textContent = safetyClear ? "READ ONLY / CONCEPTUAL ONLY" : "CHECK SAFETY FLAGS";
  safetyNode.className = `chip ${safetyClear ? "covered" : "unsafe"}`;

  $("audit-top-opportunities").textContent = number(counts.topOpportunities);
  $("audit-uncovered").textContent = number(counts.uncoveredOpportunities);
  $("audit-recent-yields").textContent = number(counts.recentIntelligentYieldCount);
  $("audit-aware-quantity").textContent = number(counts.resourceAwareStockpileQuantity);
  $("audit-path-failed").textContent = number(counts.pathFailed);
  $("audit-stockpile").textContent = counts.persistenceReady ? "READY" : "WATCH";

  $("audit-profile-table").innerHTML = profiles.map((row) => `
    <tr>
      <td>
        <div class="opportunity">${labelize(row.profile)}<small>pressure ${number(row.pressureScore)}</small></div>
      </td>
      <td><span class="chip ${row.demandState || "target"}">${labelize(row.demandState)}</span></td>
      <td>${number(row.coveredOpportunities)} covered<br><span class="resource-meta">${number(row.uncoveredOpportunities)} uncovered</span></td>
      <td>${number(row.recentYieldQuantity)} recent<br><span class="resource-meta">${number(row.resourceAwareQuantity)} aware</span></td>
      <td>
        <div class="opportunity">${labelize(row.status)}<small>${row.reason || ""}</small></div>
      </td>
    </tr>
  `).join("") || `<tr><td colspan="5"><div class="empty">No profile audit rows yet</div></td></tr>`;

  const blockerRows = [
    ["Blocked by path", blockers.blockedByPath],
    ["Blocked by density", blockers.blockedByDensity],
    ["Wrong planet", blockers.wrongPlanet],
    ["Cooldown", blockers.cooldown],
    ["Capped", blockers.capped],
    ["Remote travel", blockers.remoteTravelPending],
    ["Travel plans", blockers.travelPlansPending],
    ["Direct fallback", blockers.directFallbackUnverified],
    ["Stale validation", blockers.stalePathValidations],
    ["Target mismatch", blockers.targetMismatches],
    ["Miner off navmesh", blockers.minerNotInNavmesh],
    ["Target off navmesh", blockers.targetOutsideNavmesh],
    ["Activation failures", blockers.activationFailures],
    ["Path failed", blockers.pathFailed],
    ["Emergency disabled", blockers.emergencyDisabled ? "yes" : "no"]
  ];

  $("audit-blocker-list").innerHTML = blockerRows.map(([label, value]) => `
    <div class="safety-row">
      <span>${label}</span>
      <strong>${typeof value === "string" ? value.toUpperCase() : number(value)}</strong>
    </div>
  `).join("");
}

function renderAiPopulationTravel(ai = {}, travel = {}) {
  const zones = [...(ai.byZone || [])]
    .sort((a, b) => Number(b.activeMiners || 0) - Number(a.activeMiners || 0));
  const plans = [...(travel.plans || [])];
  const rush = travel.resourceRush || {};
  const topRemote = rush.topRemoteOpportunity || {};
  const hub = travel.homeHub || {};
  const travelImplemented = Boolean(travel.travelImplemented || ai.travelImplemented);
  const travelSupported = Boolean(travel.travelSupported || ai.travelSupported);

  $("travel-panel-state").textContent =
    `${travel.mode || "simulation-only"} · ${labelize(travel.status || ai.status || "no_data")}`;
  $("ai-pop-total").textContent = number(ai.total);
  $("ai-pop-miners").textContent = number(ai.miners || ai.activeMiners);
  $("ai-pop-assigned").textContent = number(ai.assignedMiners);
  $("ai-pop-idle").textContent = number(ai.idle);
  $("travel-plan-count").textContent = number(travel.totalPlans || plans.length);
  $("travel-remote-count").textContent = number(travel.remoteOpportunityCount || rush.remoteHighPriorityCount);

  const simulationBadge = $("travel-simulation-badge");
  simulationBadge.textContent = travelSupported ? "TRAVEL SUPPORT DETECTED" : "TRAVEL SIMULATION ONLY";
  simulationBadge.className = `chip ${travelSupported ? "watch" : "candidate"}`;

  const implementedBadge = $("travel-implemented-badge");
  implementedBadge.textContent = `TRAVEL IMPLEMENTED: ${travelImplemented ? "TRUE" : "FALSE"}`;
  implementedBadge.className = `chip ${travelImplemented ? "watch" : "blocked"}`;

  const hubBadge = $("travel-hub-badge");
  hubBadge.textContent = hub.enabled
    ? `${labelize(hub.city || "hub")} · ${labelize(hub.zone || "unknown")}`
    : "hub disabled";
  hubBadge.className = `chip ${hub.enabled ? "covered" : "no_data"}`;

  $("ai-zone-table").innerHTML = zones.map((row) => {
    const flags = [
      row.configuredMinerSpawnZone ? "configured spawn" : "no spawn config",
      row.homeHub ? "future hub" : ""
    ].filter(Boolean).join(" · ");
    const state = [
      `${number(row.sampling)} sampling`,
      `${number(row.stationed)} stationed`,
      `${number(row.moving)} moving`,
      `${number(row.idle)} idle`,
      `${number(row.blocked)} blocked`
    ].join(" · ");
    const plansText = [
      `${number(row.remotePlansFromZone)} rush`,
      `${number(row.hubPlansToZone)} hub`
    ].join(" · ");

    return `
      <tr>
        <td><div class="opportunity">${labelize(row.zone)}<small>${flags || "observed zone"}</small></div></td>
        <td>${number(row.activeMiners)}<br><span class="resource-meta">${number(row.pvp)} pvp</span></td>
        <td>${number(row.assignedMiners)}<br><span class="resource-meta">${number(row.candidateAssignments)} candidate · ${number(row.validatedAssignments)} validated</span></td>
        <td><span class="resource-meta">${state}</span></td>
        <td><span class="resource-meta">${plansText}</span></td>
      </tr>
    `;
  }).join("") || `<tr><td colspan="5"><div class="empty">No AI population zones available yet</div></td></tr>`;

  $("resource-rush-summary").innerHTML = rush.topRemoteOpportunityAvailable ? `
    <strong>${topRemote.resourceName || "unknown"}</strong>
    <span>${topRemote.resourceType || "unknown type"}</span>
    <span>${labelize(topRemote.profile)} · ${labelize(topRemote.zone || topRemote.zones || "unknown zone")}</span>
    <span>${labelize(topRemote.demandState)} · pressure ${number(topRemote.pressureScore)}</span>
    <span>${number(topRemote.localMiners)} local miners · ${topRemote.configuredSpawnZone ? "configured zone" : "no configured miner zone"}</span>
    <span>travel required · travel unsupported</span>
  ` : `
    <strong>${number(rush.localHighPriorityCount || 0)}</strong>
    <span>local high-priority opportunities</span>
    <span>${number(rush.remoteHighPriorityCount || 0)} remote high-priority opportunities</span>
    <span>travel simulation waiting for remote targets</span>
  `;

  $("travel-plan-table").innerHTML = plans.slice(0, 20).map((row) => {
    const route = `${labelize(row.currentZone)} -> ${labelize(row.targetZone || row.targetZones)}`;
    const target = row.planType === "hub_return"
      ? `${labelize(row.targetHub)}<br><span class="resource-meta">${labelize(row.purpose)} · ${labelize(row.targetCity)}</span>`
      : `${row.targetResource || "unknown"}<br><span class="resource-meta">${row.targetResourceType || "unknown type"} · ${labelize(row.selectedProfile)}</span>`;

    return `
      <tr>
        <td><span class="chip ${row.planType || "candidate"}">${labelize(row.planType)}</span></td>
        <td>#${row.minerId || "none"}</td>
        <td><span class="resource-meta">${route}</span></td>
        <td>${target}</td>
        <td>
          <div class="opportunity">${labelize(row.recommendedAction || "watch")}<small>${row.reason || ""}</small></div>
        </td>
      </tr>
    `;
  }).join("") || `<tr><td colspan="5"><div class="empty">No simulated travel plans</div></td></tr>`;
}

function formatDemandProfiles(value) {
  const text = String(value || "none");

  if (text === "none") {
    return "None";
  }

  return text.split(",").filter(Boolean).map(labelize).join(" · ");
}

function renderStockpileInspection(stockpile = {}) {
  const labelRows = [...(stockpile.labelSummaries || [])]
    .sort((a, b) => Number(b.totalKnownQuantity || 0) - Number(a.totalKnownQuantity || 0));
  const lotRows = [...(stockpile.lots || [])]
    .sort((a, b) => Number(b.quantity || 0) - Number(a.quantity || 0));
  const safetyClear = !stockpile.realResourceCreated &&
    !stockpile.resourceContainerCreated &&
    !stockpile.inventoryMutated &&
    !stockpile.economyMutated;

  $("stockpile-state").textContent = `${stockpile.status || "unavailable"} · ${number(stockpile.loadedLots)} lots`;
  $("stockpile-persistence-ready").textContent = stockpile.persistenceReady ? "READY" : "UNAVAILABLE";
  $("stockpile-checkpoint").textContent = stockpile.checkpointEnabled ? "ON" : "OFF";
  $("stockpile-demand").textContent = stockpile.persistentStockpileDemandEnabled ? "ON" : "OFF";
  $("stockpile-current").textContent = number(stockpile.currentSessionQuantity);
  $("stockpile-persisted").textContent = number(stockpile.conceptualMinerQuantity || stockpile.totalQuantity);
  $("stockpile-baseline").textContent = number(stockpile.startupBaselineQuantity);
  $("stockpile-available").textContent = `${number(stockpile.availableQuantity)} available · ${number(stockpile.reservedQuantity)} reserved`;

  const safety = $("stockpile-safety");
  safety.textContent = safetyClear ? "CONCEPTUAL ONLY" : "CHECK FLAGS";
  safety.className = `chip ${safetyClear ? "covered" : "blocked_by_path"}`;

  $("stockpile-label-table").innerHTML = labelRows.map((row) => `
    <tr>
      <td>
        <div class="opportunity">${labelize(row.label)}<small>${labelize(row.identityConfidence)} · ${labelize(row.ownerScope)}</small></div>
      </td>
      <td>${number(row.currentSessionQuantity)}</td>
      <td>${number(row.persistedQuantity)}</td>
      <td>${number(row.startupBaselineQuantity)}</td>
      <td>${number(row.totalKnownQuantity)}</td>
      <td><span class="resource-meta">${formatDemandProfiles(row.demandProfiles)}</span></td>
    </tr>
  `).join("") || `<tr><td colspan="6"><div class="empty">No conceptual stockpile labels recorded</div></td></tr>`;

  $("stockpile-lot-table").innerHTML = lotRows.slice(0, 24).map((row) => {
    const sourceName = row.sourceResourceName || row.acquisitionSource || "none";
    const sourceDetail = row.sourceResourceType || row.sourcePlanet || row.sourceZone || "conceptual";

    return `
      <tr>
        <td>${number(row.entryId)}</td>
        <td>
          <div class="opportunity">${labelize(row.conceptualLabel || "none")}<small>${labelize(row.identityConfidence)} · ${labelize(row.ownerScope)}</small></div>
        </td>
        <td>${number(row.quantity)}<br><span class="resource-meta">${number(row.availableQuantity)} available</span></td>
        <td>
          <div class="opportunity">${labelize(row.acquisitionSource)}<small>${labelize(row.resourceLifecycleState)}</small></div>
        </td>
        <td>
          <div class="opportunity">${sourceName}<small>${sourceDetail}</small></div>
        </td>
      </tr>
    `;
  }).join("") || `<tr><td colspan="5"><div class="empty">No persisted stockpile lots loaded</div></td></tr>`;
}

function renderResourceAwareStockpile(stockpile = {}) {
  const rows = [...(stockpile.rows || [])]
    .sort((a, b) => Number(b.lastObservedMs || 0) - Number(a.lastObservedMs || 0));
  const safetyClear = !stockpile.realResourceCreated &&
    !stockpile.resourceContainerCreated &&
    !stockpile.inventoryMutated &&
    !stockpile.economyMutated;

  $("resource-aware-state").textContent = `${stockpile.mode || "runtime-read-only"} · ${number(stockpile.rowCount)} rows`;
  $("resource-aware-total").textContent = number(stockpile.totalQuantity);
  $("resource-aware-rows").textContent = number(stockpile.rowCount);
  $("resource-aware-events").textContent = number(stockpile.eventCount);
  $("resource-aware-mode").textContent = stockpile.persisted ? "PERSISTED" : "RUNTIME";

  $("resource-aware-table").innerHTML = rows.slice(0, 24).map((row) => {
    const rowSafetyClear = !row.realResourceCreated &&
      !row.resourceContainerCreated &&
      !row.inventoryMutated &&
      !row.economyMutated;
    const demand = row.selectedProfile || row.selectedDemandProfile || "none";
    const density = Number(row.density || row.sourceDensity || 0);
    const densityText = density > 0 ? `${number(density)} density` : "density unknown";

    return `
      <tr>
        <td>
          <div class="opportunity">${labelize(row.conceptualLabel)}<small>${labelize(row.yieldMode || "conceptual")} · ${number(row.eventCount)} events</small></div>
        </td>
        <td>${number(row.quantity)}</td>
        <td>
          <div class="opportunity">${row.sourceResourceName || "unknown"}<small>${row.sourceResourceType || "unknown type"} · ${densityText}</small></div>
        </td>
        <td>${row.sourcePlanet || row.sourceZone || "unknown"}</td>
        <td>
          <div class="opportunity">${labelize(demand)}<small>${labelize(row.demandState || "none")} · pressure ${number(row.pressureScore)}</small></div>
        </td>
        <td><span class="resource-meta">${labelize(row.identityConfidence || "observed_resource_spawn")}</span></td>
        <td><span class="resource-meta">${number(row.lastObservedAgeSeconds)}s</span></td>
        <td><span class="chip ${rowSafetyClear ? "covered" : "blocked_by_path"}">${rowSafetyClear ? "CONCEPTUAL ONLY" : "CHECK FLAGS"}</span></td>
      </tr>
    `;
  }).join("") || `<tr><td colspan="8"><div class="empty">${safetyClear ? "No intelligent resource-aware stockpile rows yet" : "Resource-aware stockpile safety flags need review"}</div></td></tr>`;
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

  node.innerHTML = rows.slice(0, 10).map((row) => {
    const lifecycle = row.lifecycleStatus || row.status;
    const movementDetail = Number(row.activeMovementTimeoutSeconds || 0) > 0
      ? ` · move ${number(row.activeMovementAgeSeconds)}/${number(row.activeMovementTimeoutSeconds)}s`
      : "";
    const sampleDetail = Number(row.sampleTimeoutRemainingSeconds || 0) > 0
      ? ` · sample ${number(row.sampleTimeoutRemainingSeconds)}s left`
      : "";
    const protectedDetail = row.normalTtlSkippedForActiveMovement ? " · ttl protected" : "";
    return `
      <div class="assignment-row">
        <span title="${row.targetHash || row.targetResource || "none"}">${labelize(lifecycle)} - ${labelize(row.profile)}</span>
        <strong>${number(row.remainingSeconds)}s</strong>
        <small>${row.assignmentGenerationId ? `gen ${row.assignmentGenerationId}` : "gen pending"} · ${labelize(row.activationPathTrustStatus || row.latestPathTrustStatus || row.pathTrustStatus || "not_checked")}${movementDetail}${sampleDetail}${protectedDetail}</small>
      </div>
    `;
  }).join("");
}

function renderAssignmentHistory(history = {}) {
  const node = $("assignment-history-list");
  const rows = [...(history.rows || [])];

  if (rows.length === 0) {
    node.innerHTML = `<div class="empty">No cleared assignment history</div>`;
    return;
  }

  node.innerHTML = rows.slice(0, 10).map((row) => {
    const trust = row.activationPathTrustStatus || row.latestPathTrustStatus || "none";
    const status = row.lifecycleStatus || row.clearReason || "cleared";
    const movementDetail = Number(row.movementTimeoutSeconds || 0) > 0
      ? ` · move ${number(row.movementAgeSeconds)}/${number(row.movementTimeoutSeconds)}s`
      : "";
    const sampleDetail = Number(row.sampleTimeoutSeconds || 0) > 0
      ? ` · sample ${number(row.sampleAgeSeconds)}/${number(row.sampleTimeoutSeconds)}s`
      : "";
    return `
      <div class="assignment-row">
        <span title="${row.targetHash || row.targetResource || "none"}">${labelize(status)} - ${labelize(row.selectedProfile)}</span>
        <strong>${number(row.ageSeconds)}s</strong>
        <small>gen ${number(row.assignmentGenerationId)} · ${labelize(row.clearReason)} · ${labelize(trust)}${movementDetail}${sampleDetail}</small>
      </div>
    `;
  }).join("");
}

function formatTopStats(stats = {}) {
  const rows = ["OQ", "PE", "FL", "DR", "CD", "MA", "UT", "SR", "HR", "CR"]
    .map((key) => [key, Number(stats[key] || 0)])
    .filter(([, value]) => value > 0)
    .sort((a, b) => b[1] - a[1])
    .slice(0, 4);

  if (rows.length === 0) {
    return `<span class="stat-line">no stats</span>`;
  }

  return `<span class="stat-line">${rows.map(([key, value]) => `<strong>${key}</strong> ${number(value)}`).join(" · ")}</span>`;
}

function renderResourceScout(scout) {
  const available = Boolean(scout?.snapshotAvailable);
  const demandRows = [...(scout?.demandOpportunities || [])]
    .sort((a, b) => Number(b.demand?.priority || b.score || 0) - Number(a.demand?.priority || a.score || 0));
  const broadRows = [...(scout?.topBroadOpportunities || [])]
    .sort((a, b) => String(a.category || "").localeCompare(String(b.category || "")));
  const risk = scout?.risk || {};

  $("resource-scout-state").textContent = available
    ? `${scout.mode || "read-only"} · ${number(scout.activeResourceCount)} active`
    : `snapshot unavailable`;
  $("scout-active-count").textContent = number(scout?.activeResourceCount);
  $("scout-demand-count").textContent = number(scout?.demandOpportunityCount);
  $("scout-risk-density").textContent = number(risk.noDensityTargets);
  $("scout-risk-unassigned").textContent = number(risk.highValueUnassigned);

  $("resource-opportunity-list").innerHTML = demandRows.slice(0, 12).map((row) => {
    const demand = row.demand || {};
    const gather = row.gatherability || {};
    const stateGroup = demand.stateGroup || "target";
    const density = gather.knownDensityAvailable
      ? `${Math.round(Number(gather.knownDensity || 0) * 100)}%`
      : labelize(gather.confidence || "not_observed");

    return `
      <tr>
        <td>
          <div class="opportunity">${row.resourceName || "unknown"}<small>${row.resourceType || "unknown type"} · ${row.zones || row.planet || "unknown zone"}</small></div>
        </td>
        <td>${labelize(demand.profile || row.bestUse)}</td>
        <td><span class="chip ${stateGroup}">${labelize(stateGroup)}</span></td>
        <td>${number(demand.priority || row.score)}</td>
        <td>${formatTopStats(row.stats)}</td>
        <td><span class="resource-meta">${density}</span></td>
      </tr>
    `;
  }).join("") || `<tr><td colspan="6"><div class="empty">${available ? "No demand opportunities" : labelize(scout?.snapshotError || "snapshot unavailable")}</div></td></tr>`;

  $("resource-profession-list").innerHTML = broadRows.map((row) => `
    <div class="profession-row">
      <span>${labelize(row.category)}<small title="${row.resourceType || ""}">${row.resourceName || "unknown"} · ${row.resourceType || "unknown type"}</small></span>
      <strong>${number(row.score)}</strong>
    </div>
  `).join("") || `<div class="empty">No profession leaders</div>`;

  const boundaries = scout?.boundaries || {};
  const boundaryRows = [
    ["Knowledge only", boundaries.publishesKnowledgeOnly ? "yes" : "no"],
    ["Real extraction", boundaries.realExtraction ? "yes" : "no"],
    ["ResourceContainers", boundaries.resourceContainerCreation ? "yes" : "no"],
    ["Inventory mutation", boundaries.inventoryMutation ? "yes" : "no"],
    ["Market mutation", boundaries.marketMutation ? "yes" : "no"],
    ["Persistence writes", boundaries.persistenceWrites ? "yes" : "no"]
  ];

  $("scout-boundary-list").innerHTML = boundaryRows.map(([label, value]) => `
    <div class="safety-row">
      <span>${label}</span>
      <strong>${value.toUpperCase()}</strong>
    </div>
  `).join("");
}

function renderResourceCoverage(coverage) {
  const rows = [...(coverage?.opportunities || [])];
  const highest = coverage?.highestUncovered || {};
  const hasHighest = Boolean(coverage?.highestUncoveredAvailable);

  $("resource-coverage-state").textContent = rows.length
    ? `${coverage.mode || "read-only"} · ${number(rows.length)} tracked`
    : "snapshot unavailable";
  $("coverage-covered").textContent = number(coverage?.covered);
  $("coverage-uncovered").textContent = number(coverage?.uncovered);
  $("coverage-assigned").textContent = number(coverage?.assignedMiners);
  $("coverage-active").textContent = number(coverage?.activeMinerAssignments);

  $("coverage-table").innerHTML = rows.map((row) => {
    const demand = row.demand || {};
    const minerCounts = [
      Number(row.queuedMinerCount || 0) ? `${number(row.queuedMinerCount)} queued` : "",
      Number(row.movingMinerCount || 0) ? `${number(row.movingMinerCount)} moving` : "",
      Number(row.samplingMinerCount || 0) ? `${number(row.samplingMinerCount)} sampling` : ""
    ].filter(Boolean).join(" · ");
    const minerText = Number(row.assignedMinerCount || 0)
      ? `${number(row.assignedMinerCount)} assigned${minerCounts ? ` · ${minerCounts}` : ""}`
      : "none";

    return `
      <tr>
        <td>
          <div class="opportunity">${row.resourceName || "unknown"}<small>${row.resourceType || "unknown type"} · ${row.zones || row.planet || "unknown zone"}</small></div>
        </td>
        <td>${labelize(demand.profile || row.bestUse)}</td>
        <td><span class="chip ${row.coverageStatus || "uncovered"}">${labelize(row.coverageStatus || "uncovered")}</span></td>
        <td><span class="resource-meta">${minerText}</span></td>
        <td>${number(demand.priority || row.score)}</td>
        <td><span class="resource-meta">${labelize(row.coverageReason || "no active miner assigned")}</span></td>
      </tr>
    `;
  }).join("") || `<tr><td colspan="6"><div class="empty">No coverage data</div></td></tr>`;

  $("highest-uncovered").innerHTML = hasHighest ? `
    <strong>${highest.resourceName || "unknown"}</strong>
    <span>${highest.resourceType || "unknown type"}</span>
    <span>${labelize(highest.bestUse)} · ${highest.zones || highest.zone || "unknown zone"}</span>
    <span>${labelize(highest.coverageStatus)} · ${labelize(highest.reason)}</span>
    <span>Pressure ${number(highest.pressureScore)}</span>
  ` : `<div class="empty">No uncovered opportunities</div>`;
}

function renderCoveragePlanner(planner = {}) {
  const profiles = [...(planner.coverageByProfile || [])];
  const slots = [...(planner.coverageSlots || [])];
  const duration = planner.stationDurationSummary || {};
  const samples = planner.stationSampleSummary || {};

  $("coverage-planner-state").textContent =
    `${planner.mode || "memory-only"} · ${planner.stationedLifecycleEnabled ? "stationed enabled" : "stationed disabled"}`;
  $("coverage-desired-slots").textContent = number(planner.desiredCoverageSlots);
  $("coverage-actual-slots").textContent = number(planner.actualCoveredSlots);
  $("coverage-total-gap").textContent = number(planner.totalCoverageGap);
  $("coverage-stationed-miners").textContent = number(planner.stationedMiners);
  $("coverage-moving-miners").textContent = number(planner.movingMiners);
  $("coverage-sampling-miners").textContent = number(planner.samplingMiners);
  $("coverage-unassigned-miners").textContent = number(planner.unassignedMiners);

  $("coverage-profile-table").innerHTML = profiles.map((row) => {
    const movement = [
      Number(row.stationedMinerCount || 0) ? `${number(row.stationedMinerCount)} stationed` : "",
      Number(row.movingMinerCount || 0) ? `${number(row.movingMinerCount)} moving` : "",
      Number(row.samplingMinerCount || 0) ? `${number(row.samplingMinerCount)} sampling` : ""
    ].filter(Boolean).join(" · ") || "none";

    return `
      <tr>
        <td><div class="opportunity">${labelize(row.demandProfile)}<small>pressure ${number(row.pressureScore)}</small></div></td>
        <td>${number(row.desiredMiners)}</td>
        <td>${number(row.assignedMinerCount)}</td>
        <td><span class="resource-meta">${movement}</span></td>
        <td><span class="chip ${Number(row.coverageGap || 0) ? "watch" : "covered"}">${number(row.coverageGap)}</span></td>
      </tr>
    `;
  }).join("") || `<tr><td colspan="5"><div class="empty">No coverage profile rows yet</div></td></tr>`;

  $("coverage-slot-table").innerHTML = slots.slice(0, 24).map((row) => {
    const coverage = [
      Number(row.stationedMinerCount || 0) ? `${number(row.stationedMinerCount)} stationed` : "",
      Number(row.movingMinerCount || 0) ? `${number(row.movingMinerCount)} moving` : "",
      Number(row.samplingMinerCount || 0) ? `${number(row.samplingMinerCount)} sampling` : "",
      Number(row.assignedMinerCount || 0) ? `${number(row.assignedMinerCount)} assigned` : ""
    ].filter(Boolean).join(" · ") || "none";

    return `
      <tr>
        <td><div class="opportunity">${labelize(row.demandProfile)}<small>${row.coverageSlotId || "slot pending"}</small></div></td>
        <td><div class="opportunity">${row.resourceName || "unknown"}<small>${row.resourceType || row.conceptualLabel || "unknown type"}</small></div></td>
        <td>${labelize(row.zone)}</td>
        <td><span class="resource-meta">${coverage}</span></td>
        <td>${number(row.stockpileKnownQuantity)} / ${number(row.desiredReserve)}<br><span class="resource-meta">ratio ${number(row.reserveRatio)}</span></td>
        <td><span class="resource-meta">${labelize(row.rebalanceReason || "none")}</span></td>
      </tr>
    `;
  }).join("") || `<tr><td colspan="6"><div class="empty">No coverage slots yet</div></td></tr>`;

  $("coverage-station-summary").innerHTML = `
    <strong>${number(duration.stationedCount)} stationed miners</strong>
    <span>${number(duration.averageStationDurationSeconds)}s average station duration</span>
    <span>${number(duration.maxStationDurationSeconds)}s max station duration</span>
    <span>${number(samples.stationSampleCount)} station samples · ${number(samples.stationYieldQuantity)} conceptual yield</span>
    <span>Repeated sampling ${planner.stationedRepeatedSamplingEnabled ? "enabled" : "disabled"}</span>
  `;
}

function renderCoverageAlignment(diagnostics = {}) {
  const counts = diagnostics.counts || {};
  const opportunities = [...(diagnostics.opportunities || [])];
  const assignments = [...(diagnostics.assignments || [])];

  $("coverage-alignment-state").textContent = opportunities.length || assignments.length
    ? `${diagnostics.mode || "read-only"} · ${labelize(diagnostics.status || "ready")}`
    : "snapshot unavailable";
  $("alignment-exact").textContent = number(counts.opportunitiesWithExactMatch);
  $("alignment-validated").textContent = number(counts.assignmentsValidated || counts.opportunitiesWithValidatedMatch);
  $("alignment-candidate").textContent = number(counts.assignmentsCandidate || counts.opportunitiesWithCandidateMatch);
  $("alignment-untrusted").textContent = number(counts.assignmentsUntrusted || counts.opportunitiesWithUntrustedMatch);
  $("alignment-unreachable").textContent = number(
    (counts.opportunitiesWithoutConfiguredSpawnZone || 0) +
    (counts.opportunitiesTravelRequiredUnsupported || 0)
  );
  $("alignment-not-top").textContent = number(counts.assignmentsNotTopOpportunity);

  $("alignment-opportunity-table").innerHTML = opportunities.map((row) => {
    const matches = [
      `${number(row.exactProfileResourceZoneMatchCount)} exact`,
      `${number(row.resourceMatchCount)} resource`,
      `${number(row.profileMatchCount)} profile`,
      `${number(row.zoneMatchCount)} zone`
    ].join(" · ");
    const closest = Number(row.closestMinerId || 0)
      ? `#${row.closestMinerId}<br><span class="resource-meta">${labelize(row.closestAssignmentStatus)} · ${labelize(row.closestMatchReason)}</span>`
      : "none";
    const actionability = [
      row.hasActiveMinerInOpportunityZone ? "local miner zone" : "no active local miner",
      row.hasConfiguredMinerSpawnInOpportunityZone ? "configured spawn zone" : "no configured miner zone",
      row.travelRequired ? "travel required" : "same-zone reachable",
      row.travelSupported ? "travel supported" : "travel unsupported"
    ].join(" · ");

    return `
      <tr>
        <td>
          <div class="opportunity">${row.resourceName || "unknown"}<small>${row.resourceType || "unknown type"} · ${row.zones || "unknown zone"}</small></div>
        </td>
        <td>${labelize(row.profile)}<br><span class="resource-meta">${labelize(row.demandState)} · ${number(row.pressureScore)}</span></td>
        <td><span class="chip ${row.diagnosis || row.coverageStatus || "uncovered"}">${labelize(row.coverageStatus || "uncovered")}</span><br><span class="resource-meta">${labelize(row.diagnosis)}</span><br><span class="resource-meta">${actionability}</span></td>
        <td><span class="resource-meta">${matches}</span></td>
        <td>${closest}</td>
      </tr>
    `;
  }).join("") || `<tr><td colspan="5"><div class="empty">No coverage alignment opportunity rows</div></td></tr>`;

  $("alignment-assignment-table").innerHTML = assignments.map((row) => {
    const matched = Number(row.matchedTopOpportunityRank || 0)
      ? `#${number(row.matchedTopOpportunityRank)} ${row.matchedResourceName || "unknown"}<br><span class="resource-meta">${labelize(row.matchedProfile)} · ${row.matchedZones || "unknown zone"}</span>`
      : "none";
    const path = [
      labelize(row.pathValidationStatus || "unknown"),
      labelize(row.pathTrustStatus || "unknown"),
      labelize(row.densityTargetStatus || "unknown_density")
    ].join(" · ");

    return `
      <tr>
        <td>#${row.minerId || "unknown"}<br><span class="resource-meta">${number(row.ageSeconds)}s old · ${number(row.remainingSeconds)}s left</span></td>
        <td>
          <div class="opportunity">${row.assignmentResource || "unknown"}<small>${row.assignmentResourceType || "unknown type"} · ${row.assignmentZone || "unknown zone"}</small></div>
          <span class="resource-meta">${labelize(row.assignmentProfile)}</span>
        </td>
        <td>${matched}</td>
        <td><span class="chip ${row.coverageStatus || "not_top_opportunity"}">${labelize(row.coverageStatus || "not_top_opportunity")}</span><br><span class="resource-meta">${labelize(row.matchReason)}</span></td>
        <td><span class="resource-meta">${path}</span></td>
      </tr>
    `;
  }).join("") || `<tr><td colspan="5"><div class="empty">No intelligent assignments to map</div></td></tr>`;
}

function renderPathValidationDiagnostics(diagnostics = {}) {
  const rows = [...(diagnostics.rows || [])];
  const status = diagnostics.status || "no_data";
  const navmeshTerrainCount =
    Number(diagnostics.minerNotInNavmesh || 0) +
    Number(diagnostics.targetOutsideNavmesh || 0) +
    Number(diagnostics.badTerrainOrHeight || 0);
  const mismatchCount =
    Number(diagnostics.targetMismatches || 0) +
    Number(diagnostics.densityTargetCoordinateMismatches || 0);

  $("path-validation-state").textContent = rows.length
    ? `${diagnostics.mode || "read-only"} · ${labelize(status)} · ${number(diagnostics.rowCount || rows.length)} rows`
    : `${diagnostics.mode || "read-only"} · ${labelize(status)}`;
  $("path-candidates").textContent = number(diagnostics.candidateAssignments);
  $("path-failed").textContent = number(diagnostics.failedValidations);
  $("path-direct-fallback").textContent = number(diagnostics.directFallbackUnverified);
  $("path-stale").textContent = number(diagnostics.staleValidations);
  $("path-mismatch").textContent = number(mismatchCount);
  $("path-navmesh").textContent = number(navmeshTerrainCount);
  $("path-ready").textContent = number(diagnostics.forceMovementReadinessPassedCount);
  $("path-readiness-blocked").textContent = number(diagnostics.forceMovementBlockedCount);

  const trustBadge = $("path-trust-badge");
  trustBadge.textContent = `TRUST REQUIRED: ${labelize(diagnostics.pathTrustRequired || "verifiedPath").toUpperCase()}`;
  trustBadge.className = `chip ${diagnostics.pathTrustRelaxed ? "unsafe" : "blocked"}`;

  $("path-validation-table").innerHTML = rows.slice(0, 24).map((row) => {
    const minerNavmesh = row.minerInNavmeshAvailable
      ? (row.minerInNavmesh ? "miner in navmesh" : "miner outside navmesh")
      : "miner navmesh unknown";
    const targetNavmesh = row.targetInNavmeshAvailable
      ? (row.targetInNavmesh ? "target in navmesh" : "target outside navmesh")
      : "target navmesh unknown";
    const height = row.targetTerrainHeightAvailable
      ? `zΔ ${number(row.zDelta)}`
      : "height unknown";
    const pathDetail = [
      `latest ${labelize(row.latestValidationStatus || row.pathValidationStatus || "unknown")}`,
      labelize(row.latestPathTrustStatus || row.pathTrustStatus || "unknown"),
      labelize(row.rejectReason || "none")
    ].join(" · ");
    const activationDetail = [
      `activation ${labelize(row.activationValidationStatus || "none")}`,
      labelize(row.activationPathTrustStatus || "none"),
      row.activationSnapshotId ? `snap ${row.activationSnapshotId}` : "no activation snap"
    ].join(" · ");
    const distance = [
      `${number(row.straightLineDistance)}m direct`,
      `${number(row.pathDistance)}m path`,
      `${number(row.pathNodes)} nodes`
    ].join(" · ");
    const targetPosition = row.validationSnapshotAvailable
      ? `target ${number(row.targetX)}, ${number(row.targetY)}, ${number(row.targetZ)} · checked ${number(row.validationTargetX)}, ${number(row.validationTargetY)}, ${number(row.validationTargetZ)}`
      : `target ${number(row.targetX)}, ${number(row.targetY)}, ${number(row.targetZ)} · validation unavailable`;

    return `
      <tr>
        <td>#${row.minerId || "unknown"}<br><span class="resource-meta">${labelize(row.lifecycleStatus || row.assignmentStatus)} · gen ${number(row.assignmentGenerationId)} · ${number(row.assignmentAgeSeconds)}s old</span></td>
        <td>
          <div class="opportunity">${row.targetResource || "unknown"}<small>${row.targetResourceType || "unknown type"} · ${row.targetZone || "unknown zone"}</small></div>
          <span class="resource-meta">${labelize(row.selectedProfile)} · density ${number(row.density)}</span><br>
          <span class="resource-meta">${row.validationMatchesAssignment ? "snapshot matches assignment" : labelize(row.mismatchReason || "snapshot mismatch")}</span><br>
          <span class="resource-meta">${targetPosition}</span>
        </td>
        <td>
          <span class="chip ${row.explanationKey || row.pathValidationStatus || "unknown_path_failure"}">${labelize(row.explanationKey || "unknown_path_failure")}</span><br>
          <span class="resource-meta">${pathDetail}</span><br>
          <span class="resource-meta">${activationDetail}</span>
        </td>
        <td>
          ${distance}<br>
          <span class="resource-meta">drift ${number(row.coordinateMismatchDistance)}m · max ${number(row.maxPathDistance)}m/${number(row.maxPathNodes)} nodes</span>
        </td>
        <td>
          <span class="resource-meta">${minerNavmesh}</span><br>
          <span class="resource-meta">${targetNavmesh}</span><br>
          <span class="resource-meta">${height}</span>
        </td>
        <td>
          <div class="opportunity">${row.humanReason || "No path diagnostic reason available."}<small>${labelize(row.recommendedAction)} · validation age ${number(row.validationAgeSeconds)}s</small></div>
          <span class="resource-meta">${row.lifecycleDowngradePrevented ? "lifecycle downgrade prevented" : "lifecycle monotonic"}</span>
        </td>
      </tr>
    `;
  }).join("") || `<tr><td colspan="6"><div class="empty">No path validation diagnostics available yet</div></td></tr>`;
}

function renderReachabilityCalibration(calibration = {}) {
  const funnel = calibration.validationFunnel || {};
  const density = calibration.densityConversion || {};
  const outcomes = [...(calibration.validationOutcomes || [])];
  const planets = [...(calibration.byPlanet || [])];
  const distanceBands = [...(calibration.byDistanceBand || [])];
  const failures = [...(calibration.topFailureReasons || [])]
    .sort((a, b) => Number(b.count || 0) - Number(a.count || 0));
  const status = calibration.status || "no_data";

  $("reachability-state").textContent =
    `${calibration.mode || "runtime-rolling-read-only"} · ${labelize(status)}`;
  $("reachability-candidates").textContent =
    number(funnel.candidatesGenerated);
  $("reachability-validated").textContent =
    number(funnel.candidatesValidated);
  $("reachability-rejected").textContent =
    number(funnel.candidatesRejected);
  $("reachability-validation-rate").textContent =
    `${number(funnel.validationSuccessPercent)}%`;
  $("reachability-density-ratio").textContent =
    `${number(density.chosenToSampleCompletePercent)}%`;
  $("reachability-sample-complete").textContent =
    number(density.densityTargetsSampleCompleted);

  const safety = $("reachability-safety-badge");
  const readOnly = calibration.readOnly !== false &&
    !calibration.behaviorChanged &&
    !calibration.validationRelaxed &&
    !calibration.movementChanged;
  safety.textContent = readOnly ? "DIAGNOSTICS ONLY" : "CHECK BEHAVIOR FLAGS";
  safety.className = `chip ${readOnly ? "candidate" : "unsafe"}`;

  $("reachability-outcome-table").innerHTML = outcomes.map((row) => `
    <tr>
      <td><span class="chip ${row.outcome || "unknown"}">${labelize(row.outcome)}</span></td>
      <td>${number(row.count)}</td>
      <td>${number(row.percent)}%</td>
      <td>${number(row.averageDistance)}m</td>
    </tr>
  `).join("") || `<tr><td colspan="4"><div class="empty">No validation attempts recorded yet</div></td></tr>`;

  $("reachability-planet-table").innerHTML = planets.map((row) => `
    <tr>
      <td>${labelize(row.planet)}</td>
      <td>${number(row.candidates)}</td>
      <td>${number(row.validated)} / ${number(row.rejected)}</td>
      <td>${number(row.activated)}</td>
      <td>${number(row.sampleComplete)}</td>
      <td>${number(row.validationSuccessPercent)}%</td>
    </tr>
  `).join("") || `<tr><td colspan="6"><div class="empty">No planet reachability data yet</div></td></tr>`;

  $("reachability-distance-table").innerHTML = distanceBands.map((row) => `
    <tr>
      <td>${row.distanceBand || "unknown"}</td>
      <td>${number(row.candidates)}</td>
      <td>${number(row.validated)}</td>
      <td>${number(row.rejected)}</td>
      <td>${number(row.sampleComplete)}</td>
      <td>${number(row.chosenToSampleCompletePercent)}%</td>
    </tr>
  `).join("") || `<tr><td colspan="6"><div class="empty">No distance-band data yet</div></td></tr>`;

  $("reachability-failure-list").innerHTML = failures.slice(0, 8).map((row) => `
    <div class="safety-row">
      <span>${labelize(row.reason)}</span>
      <strong>${number(row.count)} · ${number(row.percent)}%</strong>
    </div>
  `).join("") || `<div class="empty">No reachability failures recorded yet</div>`;
}

function renderReachabilityMemory(memory = {}) {
  const successful = [...(memory.topSuccessfulBuckets || [])];
  const rejected = [...(memory.topRejectedBuckets || [])];
  const planets = [...(memory.byPlanet || [])];
  const resources = [...(memory.byResourceType || [])];
  const mode = memory.mode || "shadow-read-only";
  const preferenceEnabled = Boolean(memory.candidatePreferenceEnabled);

  $("reachability-memory-state").textContent =
    `${labelize(mode)} · ${labelize(memory.status || "no_data")} · bucket ${number(memory.bucketSizeMeters)}m · ttl ${number(memory.ttlSeconds)}s`;
  $("reachability-memory-rows").textContent = number(memory.memoryRows);
  $("reachability-memory-attempts").textContent = number(memory.totalAttempts);
  $("reachability-memory-verified").textContent = number(memory.verifiedPathCount);
  $("reachability-memory-rejected").textContent =
    number(memory.directFallbackUnverifiedCount);
  $("reachability-memory-sample").textContent = number(memory.sampleCompleteCount);
  $("reachability-memory-shadow-different").textContent =
    number(memory.shadowWouldSelectDifferentCount);

  const modeBadge = $("reachability-memory-mode-badge");
  modeBadge.textContent = preferenceEnabled
    ? "ACTIVE CANDIDATE PREFERENCE"
    : "SHADOW READ ONLY";
  modeBadge.className = `chip ${preferenceEnabled ? "watch" : "candidate"}`;

  $("reachability-memory-success-table").innerHTML = successful.map((row) => `
    <tr>
      <td>
        <div class="opportunity">${row.resourceName || "unknown"}<small>${row.resourceType || "unknown type"} · ${labelize(row.profile)}</small></div>
      </td>
      <td>${labelize(row.planet)} · ${number(row.bucketX)}, ${number(row.bucketY)}</td>
      <td>${number(row.verifiedPathCount)} verified<br><span class="resource-meta">${number(row.sampleCompleteCount)} complete · ${number(row.activationCount)} active</span></td>
      <td>${number(row.confidence)}<br><span class="resource-meta">density ${number(row.averageDensity)} · path ${number(row.averagePathDistance)}m</span></td>
    </tr>
  `).join("") || `<tr><td colspan="4"><div class="empty">No verified/sample-complete reachability buckets yet</div></td></tr>`;

  $("reachability-memory-rejected-table").innerHTML = rejected.map((row) => `
    <tr>
      <td>
        <div class="opportunity">${row.resourceName || "unknown"}<small>${row.resourceType || "unknown type"} · ${labelize(row.profile)}</small></div>
      </td>
      <td>${labelize(row.planet)} · ${number(row.bucketX)}, ${number(row.bucketY)}</td>
      <td>${number(row.directFallbackUnverifiedCount)} fallback<br><span class="resource-meta">${number(row.attempts)} attempts</span></td>
      <td>${number(row.lastRejectedAgeSeconds)}s<br><span class="resource-meta">confidence ${number(row.confidence)}</span></td>
    </tr>
  `).join("") || `<tr><td colspan="4"><div class="empty">No rejected reachability buckets yet</div></td></tr>`;

  $("reachability-memory-planet-table").innerHTML = planets.map((row) => `
    <tr>
      <td>${labelize(row.planet)}</td>
      <td>${number(row.attempts)}</td>
      <td>${number(row.verifiedPathCount)} · ${number(row.verifiedPercent)}%</td>
      <td>${number(row.directFallbackUnverifiedCount)}</td>
      <td>${number(row.sampleCompleteCount)} · ${number(row.sampleCompletePercent)}%</td>
    </tr>
  `).join("") || `<tr><td colspan="5"><div class="empty">No planet memory yet</div></td></tr>`;

  $("reachability-memory-resource-table").innerHTML = resources.map((row) => `
    <tr>
      <td>${labelize(row.resourceType)}</td>
      <td>${number(row.attempts)}</td>
      <td>${number(row.verifiedPathCount)} · ${number(row.verifiedPercent)}%</td>
      <td>${number(row.directFallbackUnverifiedCount)}</td>
      <td>${number(row.sampleCompleteCount)} · ${number(row.sampleCompletePercent)}%</td>
    </tr>
  `).join("") || `<tr><td colspan="5"><div class="empty">No resource-type memory yet</div></td></tr>`;
}

function renderNavAreaDensitySelection(diagnostics = {}) {
  const samples = [...(diagnostics.samples || [])];
  const reasons = [...(diagnostics.navAreaRejectionReasons || [])]
    .sort((a, b) => Number(b.count || 0) - Number(a.count || 0));
  const mode = diagnostics.mode || "shadow-read-only";
  const status = diagnostics.status || "no_data";

  $("navarea-density-state").textContent =
    `${labelize(mode)} · ${labelize(status)} · ${number(diagnostics.sampleCacheSize)} cached`;
  $("navarea-candidates").textContent = number(diagnostics.navAreaCandidatesConsidered);
  $("navarea-generated").textContent = number(diagnostics.navAreaSamplesGenerated);
  $("navarea-cache-hits").textContent = number(diagnostics.navAreaSampleCacheHits);
  $("navarea-validated").textContent = number(diagnostics.navAreaSamplesValidated);
  $("navarea-rejected").textContent = number(diagnostics.navAreaSamplesRejected);
  $("navarea-fallback").textContent = number(diagnostics.fallbackToLegacySamplingCount);

  const modeBadge = $("navarea-mode-badge");
  const active = Boolean(diagnostics.activeMode);
  modeBadge.textContent = active ? "ACTIVE NAVAREA SELECTION" : "SHADOW MODE";
  modeBadge.className = `chip ${active ? "watch" : "candidate"}`;

  $("navarea-sample-table").innerHTML = samples.map((row) => `
    <tr>
      <td>
        <div class="opportunity">${row.navAreaName || "unknown"}<small>${row.planet || "unknown planet"} · ${labelize(row.sourceRole)}</small></div>
      </td>
      <td>${number(row.x)}, ${number(row.y)}, ${number(row.z)}</td>
      <td>
        <div class="opportunity">${labelize(row.lastValidationResult)}<small>confidence ${number(row.confidence)} · age ${number(row.ageSeconds)}s</small></div>
      </td>
      <td>${number(row.useCount)} use<br><span class="resource-meta">${number(row.rejectionCount)} rejected</span></td>
    </tr>
  `).join("") || `<tr><td colspan="4"><div class="empty">No NavArea sample cache rows yet</div></td></tr>`;

  $("navarea-rejection-list").innerHTML = reasons.map((row) => `
    <div class="safety-row">
      <span>${labelize(row.reason)}</span>
      <strong>${number(row.count)}</strong>
    </div>
  `).join("") || `<div class="empty">No NavArea rejections recorded</div>`;
}

function renderRecentIntelligentYields(rows = []) {
  const recent = [...rows].slice(0, 12);

  $("recent-yield-state").textContent = recent.length
    ? `${number(recent.length)} recent`
    : "no rows";

  $("recent-yield-table").innerHTML = recent.map((row) => {
    const safetyClear = !row.realResourceCreated &&
      !row.resourceContainerCreated &&
      !row.inventoryMutated &&
      !row.economyMutated;
    const sourcePosition = row.sourceZone
      ? `${row.sourceZone} · ${number(row.sourceDensity)} density`
      : "unknown source";

    return `
      <tr>
        <td>${number(row.minerId)}</td>
        <td>
          <div class="opportunity">${number(row.amount)} ${labelize(row.conceptualLabel)}<small>${labelize(row.yieldMode || "conceptual")} · ${labelize(row.identityConfidence)}</small></div>
        </td>
        <td>
          <div class="opportunity">${row.sourceResourceName || "unknown"}<small>${row.sourceResourceType || "unknown type"} · ${sourcePosition}</small></div>
        </td>
        <td>
          <div class="opportunity">${labelize(row.selectedDemandProfile || "none")}<small>${labelize(row.demandState || "none")} · pressure ${number(row.pressureScore)}</small></div>
        </td>
        <td><span class="chip ${safetyClear ? "covered" : "blocked_by_path"}">${safetyClear ? "CONCEPTUAL ONLY" : "CHECK FLAGS"}</span></td>
        <td><span class="resource-meta">${number(row.ageSeconds)}s</span></td>
      </tr>
    `;
  }).join("") || `<tr><td colspan="6"><div class="empty">No intelligent conceptual yields recorded</div></td></tr>`;
}

function renderSnapshot(snapshot) {
  const population = snapshot.population || {};
  const activity = snapshot.minerActivity || {};
  const supply = snapshot.supply || {};

  $("as-of").textContent = snapshot.metadata?.asOfTime || "live";
  $("active-miners").textContent = number(population.activeMiners);
  $("active-pvp").textContent = number(population.activePvpBots);
  $("pvp-status").textContent = population.pvpStatus || "experimental";

  renderAiPopulationTravel(snapshot.aiPopulation || {}, snapshot.travelPlanSimulation || {});
  renderEconomyDecisionAudit(snapshot.economyDecisionAudit || {});

  $("miner-mode").textContent = `mode ${activity.mode || "off"}`;
  $("intelligent-active").textContent = number(activity.coverageActiveCount || activity.currentIntelligentActiveCount);
  $("activation-failures").textContent = number(activity.activationFailures);
  $("path-failures").textContent = number(activity.pathFailures);
  $("movement-timeouts").textContent = number(activity.movementArrivalTimeoutCount);
  $("ttl-protected").textContent = number(activity.normalTtlSkippedForActiveMovementCount);
  $("max-active").textContent = number(activity.maxActiveIntelligentMiners);
  $("cooldown").textContent = `${number(activity.cooldownSeconds)}s`;
  const readiness = snapshot.movementReadiness || {};
  const readinessStatus = readiness.movementReadinessStatus ||
    activity.movementReadinessStatus ||
    "no_data";
  const readinessBadge = $("movement-readiness-badge");
  readinessBadge.textContent = `MOVEMENT ${labelize(readinessStatus).toUpperCase()}`;
  readinessBadge.className = `chip ${readinessStatus}`;
  $("movement-readiness-passed").textContent =
    number(readiness.forceMovementReadinessPassedCount ||
      activity.forceMovementReadinessPassedCount);
  $("movement-readiness-blocked").textContent =
    number(readiness.forceMovementBlockedCount ||
      activity.forceMovementBlockedCount);
  $("movement-readiness-reason").textContent =
    labelize(readiness.movementReadinessReason ||
      activity.movementReadinessReason ||
      "waiting_for_assignment_identity");

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

  renderStockpileInspection(snapshot.stockpileInspection || {});
  renderResourceAwareStockpile(snapshot.resourceAwareStockpile || {});
  renderDemand(snapshot.demand || {});
  renderResourceScout(snapshot.resourceScout || {});
  renderResourceCoverage(snapshot.resourceCoverage || {});
  renderCoveragePlanner(snapshot.coveragePlanner || {});
  renderCoverageAlignment(snapshot.coverageAlignmentDiagnostics || {});
  renderPathValidationDiagnostics(snapshot.pathValidationDiagnostics || {});
  renderReachabilityCalibration(snapshot.reachabilityCalibration || {});
  renderReachabilityMemory(snapshot.reachabilityMemory || {});
  renderNavAreaDensitySelection(snapshot.navAreaDensitySelection || {});
  renderRecentIntelligentYields(snapshot.recentIntelligentYields || []);
  renderSafety(snapshot.safetyBoundaries || {});
  renderAssignments(activity.assignments || []);
  renderAssignmentHistory(snapshot.recentAssignmentHistory || {});
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
