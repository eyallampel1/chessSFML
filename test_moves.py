import chess

fen = "6k1/2r3pp/R7/2p3N1/1p6/5r2/PPP2P1P/5K2 b - - 2 25"
moves = "f3f6 a6a8 f6f8 a8f8 g8f8 g5e6 f8e7 e6c7 h7h5".split()
board = chess.Board(fen)
for i, mv in enumerate(moves):
    move = chess.Move.from_uci(mv)
    if move not in board.legal_moves:
        print('illegal', i, mv)
        break
    board.push(move)
else:
    print('All moves legal. Final FEN:', board.fen())
