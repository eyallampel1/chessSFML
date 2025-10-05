#ifndef GAME_H
#define GAME_H
#include "Board.h"
#include "StockfishEngine.h"
#include "EvalBar.h"
#include "Arrow.h"
#include "UserDB.h"
#include "Button.h"
#include <SFML/Graphics.hpp>
#include <SFML/Graphics/Color.hpp>
#include <SFML/Graphics/Font.hpp>
#include <SFML/Graphics/RenderWindow.hpp>
#include <SFML/Graphics/Text.hpp>
#include <SFML/System/Clock.hpp>
#include <iostream>
#include <string>
#include <vector>
#include <utility>

class Game
{
	private:
		//render text stuff
		sf::Font font;
		sf::Text text;
		std::string userWantedString;
		std::string startingPosition;
		std::string endPosition;
		bool remainWithThisColor=true;
		bool printSecTextLine=false;

		Board* board;
		sf::RenderWindow* window;
		sf::VideoMode desktop;
		sf::Vector2i getWindow;
		sf::Event event;
		sf::Clock dtClock;
		sf::Clock analysisClock;  // For periodic engine updates
		float dt;
		sf::Vector2i position;

		// Engine and analysis components
		StockfishEngine* engine;
		EvalBar* evalBar;
		ArrowManager* arrowManager;
		UserDB* userDb;
		bool engineInitialized;
		bool analysisRequested;
		std::string lastAnalyzedFEN;
        bool gameOver = false;

		// UI Buttons
		std::vector<Button*> buttons;
		Button* undoButton;
		Button* savePgnButton;
		Button* loadPgnButton;
        Button* newGameButton;
        Button* puzzleModeButton;
        Button* nextPuzzleButton;
        Button* puzzleAnalysisButton;
        Button* backToPuzzleButton;
        Button* changeCsvButton;

		// Status message
		std::string statusMessage;
		sf::Clock statusClock;

		// Engine lines for display
        std::vector<EngineLine> currentLines; 

        // Puzzle analysis toggle (default off)
        bool puzzleAnalysisEnabled = false;

        // Puzzle baseline state for returning after side-lines
        std::string puzzleStartFEN;
        std::string puzzleFirstMove;
        std::string currentPuzzleId;
	public:

		Game();
		virtual ~Game();
		void run();
		void render();
		void updateDt();
		void initWindow();
		void centerWindow();
		void processEvents();
		void printMousePosition();
		void convertMousePositionToCordinate();
		void renderText();
		void renderText(sf::Color color);
		void renderText(sf::Color color,int yPosition);
		void renderText(sf::Color color,int yPosition,std::string textToRender);

		// Engine and analysis methods
		void initEngine();
		void updateAnalysis();
		void handleKeyboard(sf::Event::KeyEvent key);

		// UI methods
		void initButtons();
		void updateButtons();
		void renderUI();
		void renderAnalysisPanel();
		void setStatusMessage(const std::string& message);
        void renderCheckmateBanner();

        // Puzzle mode state
        bool puzzleMode = false;
        bool puzzleSolved = false;
        sf::Clock puzzleSolvedClock;
        // Puzzle metadata (headers and key-value pairs for display)
        std::vector<std::string> puzzleHeaders;
        std::vector<std::pair<std::string, std::string>> puzzleMetaKVs;
        // Promotion dialog state
        bool promotionOpen = false;
        void renderPromotionDialog();
        std::string puzzleFilePath;
        std::vector<std::string> puzzleMoves;
        size_t puzzleIndex = 0; // index into puzzleMoves
        bool loadRandomPuzzle();
        void renderPuzzleSolvedBanner();
        void renderPuzzleMetadataPanel();

		// File dialog helpers
		std::string openFileDialog();
		std::string saveFileDialog();
        std::string openCSVFileDialog();

};

#endif /* GAME_H */
