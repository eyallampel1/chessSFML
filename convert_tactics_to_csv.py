import csv
import chess
import chess.pgn
from pathlib import Path

input_path = Path('pgn-tactics-generator/tactics.pgn')
output_path = Path('pgn-tactics-generator/tactics.csv')

header = [
    'PuzzleId', 'FEN', 'Moves', 'Rating', 'RatingDeviation', 'Popularity',
    'NbPlays', 'Themes', 'GameUrl', 'OpeningTags', 'Result', 'Opponent',
    'Event', 'Date', 'Comment'
]

rows = []
with input_path.open() as fh:
    index = 1
    while True:
        game = chess.pgn.read_game(fh)
        if game is None:
            break

        fen = game.headers.get('FEN', '')
        if fen:
            board = chess.Board(fen)
        else:
            board = game.board()
        move_list = []
        for move in game.mainline_moves():
            move_list.append(move.uci())
            board.push(move)

        moves_str = ' '.join(move_list)
        puzzle_id = f'PGNGEN-{index:04d}'
        rating = 1500
        rd = 75
        popularity = 0
        nb_plays = 1
        themes = 'generated'
        url = game.headers.get('Link', '')
        opening = game.headers.get('ECO', '')
        result = game.headers.get('Result', '*')
        opponent = game.headers.get('Black', '')
        event = game.headers.get('Event', '')
        date = game.headers.get('Date', '')
        comment = 'Source: pgn-tactics-generator'

        rows.append([
            puzzle_id, fen, moves_str, rating, rd, popularity, nb_plays,
            themes, url, opening, result, opponent, event, date, comment
        ])
        index += 1

with output_path.open('w', newline='') as csvfile:
    writer = csv.writer(csvfile)
    writer.writerow(header)
    writer.writerows(rows)

print(f'Wrote {len(rows)} puzzles to {output_path}')
