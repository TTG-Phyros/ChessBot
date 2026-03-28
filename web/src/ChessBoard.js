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

  const updateStatus = (boardData) => {
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
      setStatus(
        boardData.side_to_move === 1
          ? "À vous de jouer (Blanc)"
          : "Le bot réfléchit...",
      );
      setGameOver(false);
    }
  };

  const handleSquareClick = async (sq) => {
    if (gameOver || board.side_to_move !== 1) return;

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
        try {
          const response = await fetch(`${API_URL}/api/move`, {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify({ uci: move.uci }),
          });
          const data = await response.json();
          if (data.error) {
            console.error("Erreur:", data.error);
            setStatus("Coup invalide");
          } else {
            setBoard(data);
            updateStatus(data);
          }
        } catch (error) {
          console.error("Erreur:", error);
          setStatus("Erreur lors du coup");
        }
      }

      setSelectedSquare(null);
      setLegalMoves([]);
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
        <div className="board-labels">
          <div className="file-labels">
            {files.map((f) => (
              <div key={f} className="label">
                {f}
              </div>
            ))}
          </div>
          <div className="rank-labels">
            {ranks.map((r) => (
              <div key={r} className="label">
                {r}
              </div>
            ))}
          </div>
        </div>
      </div>

      <div className="controls">
        <p className="status">{status}</p>
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
