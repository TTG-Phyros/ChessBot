import React, { useState, useEffect } from "react";
import "./ChessBoard.css";
import Square from "./Square";

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

  useEffect(() => {
    fetchBoard();
  }, []);

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
    }
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
            body: JSON.stringify({ uci: move.uci }),
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
      const response = await fetch(`${API_URL}/api/reset`, { method: "POST" });
      const data = await response.json();
      setBoard(data);
      updateStatus(data);
      setSelectedSquare(null);
      setLegalMoves([]);
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

  return (
    <div className="container">
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

      <div className="controls">
        <div className="status-row" aria-live="polite">
          {isBotThinking && (
            <span className="status-spinner" aria-hidden="true" />
          )}
          <p className="status">{status}</p>
        </div>
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
    </div>
  );
}
