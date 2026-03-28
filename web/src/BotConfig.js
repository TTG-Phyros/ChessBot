import React, { useEffect, useState } from "react";
import "./BotConfig.css";

export default function BotConfig({
  onConfigChange,
  apiUrl,
  currentBot,
  currentDepth,
  currentTimeout,
  currentEval,
  currentDebug,
  currentDebugLog,
}) {
  const [activeTab, setActiveTab] = useState("settings");
  const [showConfig, setShowConfig] = useState(false);
  const [selectedBot, setSelectedBot] = useState(currentBot || "minimax");
  const [depth, setDepth] = useState(currentDepth || 4);
  const [timeout, setTimeout] = useState(currentTimeout || 30);
  const [selectedEval, setSelectedEval] = useState(currentEval || "basic");
  const [debugEnabled, setDebugEnabled] = useState(Boolean(currentDebug));
  const [debugLog, setDebugLog] = useState(currentDebugLog || "bot-search.log");
  const [isApplying, setIsApplying] = useState(false);
  const [isRunningTests, setIsRunningTests] = useState(false);
  const [testStatus, setTestStatus] = useState("");
  const [testState, setTestState] = useState("idle");
  const [lastRunLabel, setLastRunLabel] = useState("");
  const [testResult, setTestResult] = useState(null);

  useEffect(() => {
    setSelectedBot(currentBot || "minimax");
    setDepth(currentDepth || 4);
    setTimeout(currentTimeout || 30);
    setSelectedEval(currentEval || "basic");
    setDebugEnabled(Boolean(currentDebug));
    setDebugLog(currentDebugLog || "bot-search.log");
  }, [
    currentBot,
    currentDepth,
    currentTimeout,
    currentEval,
    currentDebug,
    currentDebugLog,
  ]);

  const sanitizeDepth = (value) => {
    const parsed = Number.parseInt(value, 10);
    if (Number.isNaN(parsed)) {
      return 4;
    }
    return Math.min(8, Math.max(1, parsed));
  };

  const sanitizeTimeout = (value) => {
    const parsed = Number.parseInt(value, 10);
    if (Number.isNaN(parsed)) {
      return 30;
    }
    return Math.min(300, Math.max(1, parsed));
  };

  const runTests = async (suite) => {
    setIsRunningTests(true);
    setTestState("running");
    setTestStatus("Execution des tests...");
    setTestResult(null);

    try {
      const response = await fetch(`${apiUrl}/api/tests`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({
          bot: selectedBot,
          suite,
          perftMaxDepth: Math.min(6, sanitizeDepth(depth)),
          timeout: sanitizeTimeout(timeout),
        }),
      });
      const payload = await response.json();
      setTestResult(payload);
      setTestState(payload.passed ? "passed" : "failed");
      setTestStatus(
        payload.passed
          ? "Tous les tests sont passes"
          : "Certains tests ont echoue",
      );
      setLastRunLabel(new Date().toLocaleTimeString("fr-FR"));
    } catch (error) {
      console.error("Erreur tests:", error);
      setTestState("error");
      setTestStatus("Erreur lors de l'execution des tests");
    } finally {
      setIsRunningTests(false);
    }
  };

  const normalizeOutput = (rawOutput) => {
    if (!rawOutput) {
      return [];
    }
    return rawOutput
      .split("\n")
      .map((line) => line.trimEnd())
      .filter((line) => line.length > 0);
  };

  const outputSeverityClass = (line) => {
    const lower = line.toLowerCase();
    if (
      lower.includes("mismatch") ||
      lower.includes("error") ||
      lower.includes("failed")
    ) {
      return "line-error";
    }
    if (lower.includes("passed") || lower.includes("ok")) {
      return "line-success";
    }
    if (
      lower.includes("depth") ||
      lower.includes("nodes=") ||
      lower.includes("nps=")
    ) {
      return "line-metric";
    }
    return "line-normal";
  };

  const testStateLabel = {
    idle: "Aucun test lance",
    running: "Tests en cours",
    passed: "Tests OK",
    failed: "Tests en echec",
    error: "Erreur",
  };

  const handleApply = async () => {
    const nextConfig = {
      bot: selectedBot,
      depth: sanitizeDepth(depth),
      timeout: sanitizeTimeout(timeout),
      eval: selectedEval || "basic",
      debug: Boolean(debugEnabled),
      debugLog: (debugLog || "bot-search.log").trim() || "bot-search.log",
    };

    setIsApplying(true);
    try {
      await onConfigChange(nextConfig);
      setShowConfig(false);
    } finally {
      setIsApplying(false);
    }
  };

  const handleReset = () => {
    setSelectedBot(currentBot || "minimax");
    setDepth(currentDepth || 4);
    setTimeout(currentTimeout || 30);
    setSelectedEval(currentEval || "basic");
    setDebugEnabled(Boolean(currentDebug));
    setDebugLog(currentDebugLog || "bot-search.log");
  };

  return (
    <div className="bot-config">
      <button
        className="config-toggle"
        onClick={() => setShowConfig(!showConfig)}
        title="Configure bot and parameters"
      >
        ⚙️ Configuration
      </button>

      {showConfig && (
        <div className="config-panel">
          <div className="config-header">
            <h2>Configuration du Bot</h2>
            <button
              className="close-btn"
              onClick={() => setShowConfig(false)}
              aria-label="Close config"
            >
              ✕
            </button>
          </div>

          <div
            className="tab-bar"
            role="tablist"
            aria-label="Configuration et tests"
          >
            <button
              className={`tab-btn ${activeTab === "settings" ? "active" : ""}`}
              onClick={() => setActiveTab("settings")}
              role="tab"
              aria-selected={activeTab === "settings"}
            >
              Parametres
            </button>
            <button
              className={`tab-btn ${activeTab === "tests" ? "active" : ""}`}
              onClick={() => setActiveTab("tests")}
              role="tab"
              aria-selected={activeTab === "tests"}
            >
              Tests
            </button>
          </div>

          <div className="config-content">
            {activeTab === "settings" && (
              <>
                <div className="config-group">
                  <label htmlFor="bot-select">Selectionner le bot:</label>
                  <select
                    id="bot-select"
                    value={selectedBot}
                    onChange={(e) => setSelectedBot(e.target.value)}
                  >
                    <option value="minimax">MiniMax (Alpha-Beta)</option>
                    <option value="iterative-deepening">
                      Iterative Deepening
                    </option>
                  </select>
                  <p className="config-description">
                    {selectedBot === "minimax"
                      ? "Recherche standard avec elagage alpha-beta jusqu'a une profondeur fixe."
                      : "Approfondissement iteratif: cherche progressivement plus profond pour un meilleur classement des coups."}
                  </p>
                </div>

                <div className="config-group">
                  <label htmlFor="depth-slider">
                    Profondeur de recherche: {depth}
                  </label>
                  <input
                    id="depth-slider"
                    type="range"
                    min="1"
                    max="8"
                    value={depth}
                    onChange={(e) => setDepth(e.target.value)}
                    className="slider"
                  />
                  <div className="depth-labels">
                    <span>1 (Rapide)</span>
                    <span>8 (Profond)</span>
                  </div>
                </div>

                <div className="config-group">
                  <label htmlFor="timeout-input">
                    Temps limite (secondes):
                  </label>
                  <input
                    id="timeout-input"
                    type="number"
                    min="1"
                    max="300"
                    value={timeout}
                    onChange={(e) => setTimeout(e.target.value)}
                    className="number-input"
                  />
                </div>

                <div className="config-group">
                  <label htmlFor="eval-select">Profil d'evaluation:</label>
                  <select
                    id="eval-select"
                    value={selectedEval}
                    onChange={(e) => setSelectedEval(e.target.value)}
                  >
                    <option value="basic">Basic (materiel)</option>
                    <option value="advanced">
                      Advanced (materiel + position + mobilite)
                    </option>
                    <option value="tactical">
                      Tactical (pression tactique + attaques)
                    </option>
                  </select>
                </div>

                <div className="config-group">
                  <label className="checkbox-label" htmlFor="debug-enabled">
                    <input
                      id="debug-enabled"
                      type="checkbox"
                      checked={debugEnabled}
                      onChange={(e) => setDebugEnabled(e.target.checked)}
                    />
                    Activer les logs de debug (profondeur 1..N)
                  </label>
                </div>

                <div className="config-group">
                  <label htmlFor="debug-log-input">Fichier log debug:</label>
                  <input
                    id="debug-log-input"
                    type="text"
                    value={debugLog}
                    onChange={(e) => setDebugLog(e.target.value)}
                    className="text-input"
                    placeholder="bot-search.log"
                  />
                </div>

                <div className="config-info">
                  <p>
                    <strong>Bot actuel:</strong>{" "}
                    {currentBot === "minimax"
                      ? "MiniMax"
                      : "Iterative Deepening"}
                  </p>
                  <p>
                    <strong>Profondeur:</strong> {currentDepth}
                  </p>
                  <p>
                    <strong>Timeout:</strong> {currentTimeout}s
                  </p>
                  <p>
                    <strong>Eval:</strong> {currentEval || "basic"}
                  </p>
                  <p>
                    <strong>Debug:</strong>{" "}
                    {currentDebug ? "active" : "desactive"}
                  </p>
                  <p>
                    <strong>Log:</strong> {currentDebugLog || "bot-search.log"}
                  </p>
                </div>
              </>
            )}

            {activeTab === "tests" && (
              <div className="test-panel tab-test-panel">
                <div className="test-panel-head">
                  <h3>Tests du Bot</h3>
                  <span className={`status-badge ${testState}`}>
                    {testStateLabel[testState]}
                  </span>
                </div>

                <p className="test-description">
                  Lance les tests du bot selectionne dans cet onglet.
                </p>
                <p className="test-meta">
                  Bot cible: <strong>{selectedBot}</strong>
                  {lastRunLabel ? ` • Dernier lancement: ${lastRunLabel}` : ""}
                </p>

                <div className="test-button-row">
                  <button
                    className="btn-test"
                    onClick={() => runTests("unit")}
                    disabled={isRunningTests}
                  >
                    Test unitaires
                  </button>
                  <button
                    className="btn-test"
                    onClick={() => runTests("perft")}
                    disabled={isRunningTests}
                  >
                    Test perft
                  </button>
                  <button
                    className="btn-test"
                    onClick={() => runTests("all")}
                    disabled={isRunningTests}
                  >
                    Tous les tests
                  </button>
                </div>

                {testStatus && <p className="test-status">{testStatus}</p>}

                {testResult && Array.isArray(testResult.tests) && (
                  <div className="test-results">
                    {testResult.tests.map((test) => {
                      const outputLines = normalizeOutput(test.output);

                      return (
                        <div key={test.name} className="test-result-card">
                          <div className="test-result-header">
                            <p className="test-result-title">{test.name}</p>
                            <span
                              className={`status-badge small ${test.passed ? "passed" : "failed"}`}
                            >
                              {test.passed ? "OK" : "ECHEC"}
                            </span>
                          </div>

                          <p className="test-command">
                            Commande: {test.command}
                          </p>
                          <p className="test-exit">
                            Exit code: {test.exitCode}
                          </p>

                          <div className="test-output">
                            {outputLines.length === 0 && (
                              <div className="output-line line-normal">
                                (aucune sortie)
                              </div>
                            )}
                            {outputLines.map((line, index) => (
                              <div
                                key={`${test.name}-${index}`}
                                className={`output-line ${outputSeverityClass(line)}`}
                              >
                                {line}
                              </div>
                            ))}
                          </div>
                        </div>
                      );
                    })}
                  </div>
                )}
              </div>
            )}
          </div>

          <div className="config-buttons">
            <button className="btn-cancel" onClick={handleReset}>
              Annuler
            </button>
            <button className="btn-apply" onClick={handleApply}>
              {isApplying ? "Application..." : "Appliquer"}
            </button>
          </div>
        </div>
      )}
    </div>
  );
}
