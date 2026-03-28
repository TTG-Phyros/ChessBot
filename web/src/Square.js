import React from "react";
import "./Square.css";

export default function Square({
  sq,
  piece,
  symbol,
  isLight,
  isSelected,
  isLegalTarget,
  onClick,
}) {
  let className = "square";
  if (isLight) className += " light";
  else className += " dark";
  if (isSelected) className += " selected";
  if (isLegalTarget) className += " legal-target";

  return (
    <button className={className} onClick={onClick}>
      <span className="piece">{symbol}</span>
    </button>
  );
}
