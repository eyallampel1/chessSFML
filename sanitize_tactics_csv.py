import csv
import chess
from pathlib import Path

path = Path('pgn-tactics-generator/tactics.csv')
rows = []
with path.open() as f:
    reader = csv.DictReader(f)
    fieldnames = reader.fieldnames
    for row in reader:
        fen = row['FEN']
        board = chess.Board(fen)
        move_tokens = row['Moves'].split()
        sanitized = []
        for mv in move_tokens:
            move = chess.Move.from_uci(mv)
            if move not in board.legal_moves:
                # illegal move likely means puzzle already finished; stop processing further moves
                break
            sanitized.append(mv)
            board.push(move)
            if board.is_game_over(claim_draw=True):
                break
        row['Moves'] = ' '.join(sanitized)
        rows.append(row)

with path.open('w', newline='') as f:
    writer = csv.DictWriter(f, fieldnames=fieldnames)
    writer.writeheader()
    writer.writerows(rows)

print(f'Sanitized {len(rows)} puzzles in {path}')
