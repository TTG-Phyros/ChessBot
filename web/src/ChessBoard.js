import React, { useState, useEffect } from "react";
import "./ChessBoard.css";
import Square from "./Square";
import BotConfig from "./BotConfig";

const PIECE_SYMBOLS = {
  1: "♙",
  "-1": "♟", // Pawns
  2: "♘",
  "-2": "♞", // Knights
  3: "♗",
  "-3": "♝", // Bishops
  4: "♖",
  "-4": "♜", // Rooks
  5: "♕",
  "-5": "♛", // Queens
  6: "♔",
  "-6": "♚", // Kings
  0: "",
};

const PIECE_VALUES = {
  1: 1,
  "-1": 1,
  2: 3,
  "-2": 3,
  3: 3,
  "-3": 3,
  4: 5,
  "-4": 5,
  5: 9,
  "-5": 9,
};

const API_URL = (() => {
  if (process.env.REACT_APP_API_URL) {
    return process.env.REACT_APP_API_URL;
  }
  // Fall back to auto-detecting the API URL from host
  const protocol = window.location.protocol;
  const hostname = window.location.hostname;
  return `${protocol}//${hostname}:5000`;
})();

export default function ChessBoard() {
  const [board, setBoard] = useState(null);
  const [selectedSquare, setSelectedSquare] = useState(null);
  const [legalMoves, setLegalMoves] = useState([]);
  const [status, setStatus] = useState("Chargement...");
  const [gameOver, setGameOver] = useState(false);
  const [isFlipped, setIsFlipped] = useState(false);
  const [isBotThinking, setIsBotThinking] = useState(false);
  const [showPgnTagsPopup, setShowPgnTagsPopup] = useState(false);
  const [pgnPopupShownForCurrentGame, setPgnPopupShownForCurrentGame] =
    useState(false);
  const [isSavingPgnTags, setIsSavingPgnTags] = useState(false);
  const [pgnTagStatus, setPgnTagStatus] = useState("");
  const [pgnTags, setPgnTags] = useState({
    event: "",
    site: "",
    white: "",
    black: "",
    round: "?",
    timeControl: "",
    whiteElo: "",
    blackElo: "",
    termination: "",
    eco: "",
    endTime: "",
    link: "",
  });
  const [botConfig, setBotConfig] = useState({
    bot: "minimax",
    depth: 4,
    timeout: 30,
    eval: "basic",
    debug: false,
    debugLog: "bot-search.log",
  });

  const applyUserMoveLocally = (currentBoard, move) => {
    const nextBoard = {
      ...currentBoard,
      squares: [...currentBoard.squares],
      legal_moves: [],
      move_count: 0,
      side_to_move: -1,
    };
    const movingPiece = nextBoard.squares[move.from];
    const fromFile = move.from % 8;
    const toFile = move.to % 8;
    const promotion = move.uci.length === 5 ? move.uci[4] : null;

    nextBoard.squares[move.from] = 0;

    if (movingPiece === 6 && move.from === 4 && move.to === 6) {
      nextBoard.squares[7] = 0;
      nextBoard.squares[5] = 4;
    } else if (movingPiece === 6 && move.from === 4 && move.to === 2) {
      nextBoard.squares[0] = 0;
      nextBoard.squares[3] = 4;
    }

    if (
      movingPiece === 1 &&
      Math.abs(fromFile - toFile) === 1 &&
      nextBoard.squares[move.to] === 0
    ) {
      nextBoard.squares[move.to - 8] = 0;
    }

    if (promotion) {
      const promoMap = { q: 5, r: 4, b: 3, n: 2 };
      nextBoard.squares[move.to] =
        promoMap[promotion.toLowerCase()] || movingPiece;
    } else {
      nextBoard.squares[move.to] = movingPiece;
    }

    return nextBoard;
  };

  const STARTING_PIECE_COUNTS = {
    white: { 1: 8, 2: 2, 3: 2, 4: 2, 5: 1 },
    black: { "-1": 8, "-2": 2, "-3": 2, "-4": 2, "-5": 1 },
  };

  const getCurrentPieceCounts = (squares) => {
    const counts = {
      white: { 1: 0, 2: 0, 3: 0, 4: 0, 5: 0 },
      black: { "-1": 0, "-2": 0, "-3": 0, "-4": 0, "-5": 0 },
    };

    squares.forEach((piece) => {
      if (piece >= 1 && piece <= 5) {
        counts.white[piece] += 1;
      }
      if (piece <= -1 && piece >= -5) {
        counts.black[piece] += 1;
      }
    });

    return counts;
  };

  const buildCapturedList = (startCounts, currentCounts, pieceOrder) => {
    const captured = [];

    pieceOrder.forEach((piece) => {
      const missing = startCounts[piece] - currentCounts[piece];
      for (let i = 0; i < missing; i += 1) {
        captured.push(piece);
      }
    });

    return captured;
  };

  useEffect(() => {
    fetchBoard();
    fetchBotConfig();
  }, []);

  const fetchBotConfig = async () => {
    try {
      const response = await fetch(`${API_URL}/api/config`);
      const data = await response.json();
      if (!data.error) {
        setBotConfig((prev) => ({
          ...prev,
          bot: data.bot || prev.bot,
          depth: Number.isInteger(data.depth) ? data.depth : prev.depth,
          eval: data.eval || prev.eval,
          debug: Boolean(data.debug),
          debugLog: data.debugLog || prev.debugLog,
        }));
      }
    } catch (error) {
      console.error("Erreur de recuperation de la configuration bot:", error);
    }
  };

  const handleConfigChange = async (newConfig) => {
    setBotConfig(newConfig);

    try {
      const response = await fetch(`${API_URL}/api/reset`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({
          bot: newConfig.bot,
          depth: newConfig.depth,
          timeout: newConfig.timeout,
          eval: newConfig.eval,
          debug: newConfig.debug,
          debugLog: newConfig.debugLog,
        }),
      });
      const data = await response.json();
      if (data.error) {
        setStatus(`Configuration non appliquee: ${data.error}`);
        return;
      }

      setBoard(data);
      updateStatus(data);
      setSelectedSquare(null);
      setLegalMoves([]);
      setGameOver(false);
      setShowPgnTagsPopup(false);
      setPgnPopupShownForCurrentGame(false);
      setPgnTagStatus("");
      setStatus(
        `Configuration appliquee (${newConfig.bot}, profondeur ${newConfig.depth})`,
      );
    } catch (error) {
      console.error("Erreur lors de l'application de la configuration:", error);
      setStatus("Erreur lors de l'application de la configuration");
    }
  };

  const fetchBoard = async () => {
    try {
      const response = await fetch(`${API_URL}/api/board`);
      const data = await response.json();
      setBoard(data);
      updateStatus(data);
      setGameOver(data.move_count === 0 && !data.in_check === false);
    } catch (error) {
      console.error("Erreur de connexion:", error);
      setStatus("Erreur de connexion au serveur");
    }
  };

  const updateStatus = (boardData, lastEngineMove = null) => {
    if (boardData.move_count === 0) {
      if (boardData.in_check) {
        setStatus(
          boardData.side_to_move === 1
            ? "Échec et mat - Noir gagne"
            : "Échec et mat - Blanc gagne",
        );
      } else {
        setStatus("Pat");
      }
      setGameOver(true);
      if (!pgnPopupShownForCurrentGame) {
        setShowPgnTagsPopup(true);
        setPgnPopupShownForCurrentGame(true);
      }
    } else {
      if (boardData.side_to_move === 1) {
        if (lastEngineMove) {
          setStatus(`Le bot a joué ${lastEngineMove}. À vous de jouer (Blanc)`);
        } else {
          setStatus("À vous de jouer (Blanc)");
        }
      } else {
        setStatus("Le bot réfléchit...");
      }
      setGameOver(false);
      setPgnPopupShownForCurrentGame(false);
    }
  };

  const handlePgnTagFieldChange = (field, value) => {
    setPgnTags((prev) => ({
      ...prev,
      [field]: value,
    }));
  };

  const submitOptionalPgnTags = async () => {
    setIsSavingPgnTags(true);
    setPgnTagStatus("");

    try {
      const response = await fetch(`${API_URL}/api/pgn-tags`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(pgnTags),
      });
      const payload = await response.json();

      if (!response.ok || payload.error) {
        setPgnTagStatus(
          payload.error || "Impossible d'enregistrer les tags optionnels.",
        );
        return;
      }

      setPgnTagStatus(
        `Tags optionnels enregistres (${payload.writtenTags || 0})`,
      );
      setShowPgnTagsPopup(false);
    } catch (error) {
      console.error("Erreur envoi tags PGN:", error);
      setPgnTagStatus("Erreur reseau lors de l'enregistrement des tags.");
    } finally {
      setIsSavingPgnTags(false);
    }
  };

  const skipOptionalPgnTags = () => {
    setShowPgnTagsPopup(false);
    setPgnTagStatus("Tags optionnels ignores pour cette partie.");
  };

  const handleSquareClick = async (sq) => {
    if (gameOver || board.side_to_move !== 1 || isBotThinking) return;

    if (selectedSquare === null) {
      const moves = board.legal_moves.filter((m) => m.from === sq);
      if (moves.length > 0) {
        setSelectedSquare(sq);
        setLegalMoves(moves);
      }
    } else {
      const move = board.legal_moves.find(
        (m) => m.from === selectedSquare && m.to === sq,
      );

      if (move) {
        const previousBoard = board;
        const optimisticBoard = applyUserMoveLocally(board, move);

        setBoard(optimisticBoard);
        setStatus("Le bot réfléchit...");
        setIsBotThinking(true);
        setSelectedSquare(null);
        setLegalMoves([]);

        try {
          const response = await fetch(`${API_URL}/api/move`, {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify({
              uci: move.uci,
              bot: botConfig.bot,
              depth: botConfig.depth,
              timeout: botConfig.timeout,
              eval: botConfig.eval,
              debug: botConfig.debug,
              debugLog: botConfig.debugLog,
            }),
          });
          const data = await response.json();
          if (data.error) {
            console.error("Erreur:", data.error);
            setBoard(previousBoard);
            setStatus("Coup invalide");
          } else {
            setBoard(data);
            updateStatus(data, data.engine_move || null);
          }
        } catch (error) {
          console.error("Erreur:", error);
          setBoard(previousBoard);
          setStatus("Erreur lors du coup");
        } finally {
          setIsBotThinking(false);
        }
      }

      if (!isBotThinking) {
        setSelectedSquare(null);
        setLegalMoves([]);
      }
    }
  };

  const handleReset = async () => {
    try {
      const response = await fetch(`${API_URL}/api/reset`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({
          bot: botConfig.bot,
          depth: botConfig.depth,
          timeout: botConfig.timeout,
          eval: botConfig.eval,
          debug: botConfig.debug,
          debugLog: botConfig.debugLog,
        }),
      });
      const data = await response.json();
      setBoard(data);
      updateStatus(data);
      setSelectedSquare(null);
      setLegalMoves([]);
      setShowPgnTagsPopup(false);
      setPgnPopupShownForCurrentGame(false);
      setPgnTagStatus("");
    } catch (error) {
      console.error("Erreur:", error);
    }
  };

  if (!board) {
    return (
      <div className="container">
        <p>{status}</p>
      </div>
    );
  }

  const isLightSquare = (sq) => {
    const file = sq % 8;
    const rank = Math.floor(sq / 8);
    return (file + rank) % 2 === 0;
  };

  const files = isFlipped
    ? ["h", "g", "f", "e", "d", "c", "b", "a"]
    : ["a", "b", "c", "d", "e", "f", "g", "h"];
  const ranks = isFlipped
    ? ["1", "2", "3", "4", "5", "6", "7", "8"]
    : ["8", "7", "6", "5", "4", "3", "2", "1"];

  const displayedSquares = [];
  for (let row = 0; row < 8; row++) {
    for (let col = 0; col < 8; col++) {
      const rank = isFlipped ? row : 7 - row;
      const file = isFlipped ? 7 - col : col;
      displayedSquares.push(rank * 8 + file);
    }
  }

  const currentCounts = getCurrentPieceCounts(board.squares);

  const capturedByWhite = buildCapturedList(
    STARTING_PIECE_COUNTS.black,
    currentCounts.black,
    [-5, -4, -3, -2, -1],
  );

  const capturedByBlack = buildCapturedList(
    STARTING_PIECE_COUNTS.white,
    currentCounts.white,
    [5, 4, 3, 2, 1],
  );

  const materialCapturedByWhite = capturedByWhite.reduce(
    (total, piece) => total + (PIECE_VALUES[String(piece)] || 0),
    0,
  );

  const materialCapturedByBlack = capturedByBlack.reduce(
    (total, piece) => total + (PIECE_VALUES[String(piece)] || 0),
    0,
  );

  const materialDelta = materialCapturedByWhite - materialCapturedByBlack;
  const materialLabel =
    materialDelta > 0
      ? `Blanc +${materialDelta}`
      : materialDelta < 0
        ? `Noir +${Math.abs(materialDelta)}`
        : "Egalite";

  return (
    <div className="container">
      <div className="board-and-recap">
        <div className="board-wrapper">
          <div className="board">
            {displayedSquares.map((sq) => {
              const piece = board.squares[sq];
              const isLight = isLightSquare(sq);
              const isSelected = sq === selectedSquare;
              const isLegalTarget = legalMoves.some((m) => m.to === sq);

              return (
                <Square
                  key={sq}
                  sq={sq}
                  piece={piece}
                  symbol={PIECE_SYMBOLS[String(piece)]}
                  isLight={isLight}
                  isSelected={isSelected}
                  isLegalTarget={isLegalTarget}
                  onClick={() => handleSquareClick(sq)}
                />
              );
            })}
          </div>
        </div>

        <aside className="captures-panel" aria-label="Pieces capturees">
          <div className="captures-header">
            <h3>Pieces capturees</h3>
            <span
              className={`material-badge ${
                materialDelta > 0
                  ? "white-adv"
                  : materialDelta < 0
                    ? "black-adv"
                    : "even"
              }`}
            >
              {materialLabel}
            </span>
          </div>

          <div className="capture-row">
            <p className="capture-title">Par Blanc</p>
            <div className="capture-list" aria-label="Pieces noires capturees">
              {capturedByWhite.length > 0 ? (
                capturedByWhite.map((piece, index) => (
                  <span
                    key={`w-${piece}-${index}`}
                    className="capture-piece dark-piece"
                  >
                    {PIECE_SYMBOLS[String(piece)]}
                  </span>
                ))
              ) : (
                <span className="capture-empty">Aucune</span>
              )}
            </div>
          </div>

          <div className="capture-row">
            <p className="capture-title">Par Noir</p>
            <div
              className="capture-list"
              aria-label="Pieces blanches capturees"
            >
              {capturedByBlack.length > 0 ? (
                capturedByBlack.map((piece, index) => (
                  <span
                    key={`b-${piece}-${index}`}
                    className="capture-piece light-piece"
                  >
                    {PIECE_SYMBOLS[String(piece)]}
                  </span>
                ))
              ) : (
                <span className="capture-empty">Aucune</span>
              )}
            </div>
          </div>
        </aside>
      </div>

      <div className="controls">
        <div className="status-row" aria-live="polite">
          {isBotThinking && (
            <span className="status-spinner" aria-hidden="true" />
          )}
          <p className="status">{status}</p>
        </div>
        {pgnTagStatus && <p className="pgn-tag-status">{pgnTagStatus}</p>}
        <BotConfig
          onConfigChange={handleConfigChange}
          apiUrl={API_URL}
          currentBot={botConfig.bot}
          currentDepth={botConfig.depth}
          currentTimeout={botConfig.timeout}
          currentEval={botConfig.eval}
          currentDebug={botConfig.debug}
          currentDebugLog={botConfig.debugLog}
        />
        <div className="button-row">
          <button
            onClick={() => setIsFlipped((prev) => !prev)}
            className="rotate-btn"
          >
            Tourner l'echiquier
          </button>
          <button onClick={handleReset} className="reset-btn">
            Nouvelle Partie (R)
          </button>
        </div>
      </div>

      {showPgnTagsPopup && (
        <div className="pgn-popup-overlay" role="dialog" aria-modal="true">
          <div className="pgn-popup-card">
            <h3>Tags PGN optionnels</h3>
            <p>
              La partie est terminee. Tu peux renseigner des tags optionnels a
              ajouter au log.
            </p>

            <div className="pgn-form-grid">
              <label>
                Event
                <input
                  type="text"
                  value={pgnTags.event}
                  onChange={(e) => handlePgnTagFieldChange("event", e.target.value)}
                />
              </label>
              <label>
                Site
                <input
                  type="text"
                  value={pgnTags.site}
                  onChange={(e) => handlePgnTagFieldChange("site", e.target.value)}
                />
              </label>
              <label>
                White
                <input
                  type="text"
                  value={pgnTags.white}
                  onChange={(e) => handlePgnTagFieldChange("white", e.target.value)}
                />
              </label>
              <label>
                Black
                <input
                  type="text"
                  value={pgnTags.black}
                  onChange={(e) => handlePgnTagFieldChange("black", e.target.value)}
                />
              </label>
              <label>
                Round
                <input
                  type="text"
                  value={pgnTags.round}
                  onChange={(e) => handlePgnTagFieldChange("round", e.target.value)}
                />
              </label>
              <label>
                TimeControl
                <input
                  type="text"
                  value={pgnTags.timeControl}
                  onChange={(e) => handlePgnTagFieldChange("timeControl", e.target.value)}
                />
              </label>
              <label>
                WhiteElo
                <input
                  type="text"
                  value={pgnTags.whiteElo}
                  onChange={(e) => handlePgnTagFieldChange("whiteElo", e.target.value)}
                />
              </label>
              <label>
                BlackElo
                <input
                  type="text"
                  value={pgnTags.blackElo}
                  onChange={(e) => handlePgnTagFieldChange("blackElo", e.target.value)}
                />
              </label>
              <label>
                Termination
                <input
                  type="text"
                  value={pgnTags.termination}
                  onChange={(e) =>
                    handlePgnTagFieldChange("termination", e.target.value)
                  }
                />
              </label>
              <label>
                ECO
                <input
                  type="text"
                  value={pgnTags.eco}
                  onChange={(e) => handlePgnTagFieldChange("eco", e.target.value)}
                />
              </label>
              <label>
                EndTime
                <input
                  type="text"
                  value={pgnTags.endTime}
                  onChange={(e) => handlePgnTagFieldChange("endTime", e.target.value)}
                />
              </label>
              <label className="pgn-full-width">
                Link
                <input
                  type="text"
                  value={pgnTags.link}
                  onChange={(e) => handlePgnTagFieldChange("link", e.target.value)}
                />
              </label>
            </div>

            <div className="pgn-popup-actions">
              <button
                type="button"
                className="pgn-btn-secondary"
                onClick={skipOptionalPgnTags}
                disabled={isSavingPgnTags}
              >
                Passer
              </button>
              <button
                type="button"
                className="pgn-btn-primary"
                onClick={submitOptionalPgnTags}
                disabled={isSavingPgnTags}
              >
                {isSavingPgnTags ? "Enregistrement..." : "Enregistrer les tags"}
              </button>
            </div>
          </div>
        </div>
      )}
    </div>
  );
}
