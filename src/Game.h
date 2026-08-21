#ifndef GAME_H
#define GAME_H
#include "Board.h"
#include "StockfishEngine.h"
#include "EvalBar.h"
#include "Arrow.h"
#include "UserDB.h"
#include "Button.h"
#include "CourseTrainer.h"
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
#include <thread>
#include <atomic>
#include <mutex>

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
        Button* analyzePgnButton;
        Button* courseButton;

        // Opening-course trainer (Hakobyan 1.d4). Owns its own panel, buttons
        // and board navigation; the Game just forwards events to it.
        CourseTrainer* course = nullptr;
        sf::Vector2u normalWindowSize;

        // Course mode draws the board larger. Rather than rescale every
        // hard-coded 44-pixel square, the board is drawn through its own view:
        // board code keeps working in 0..352 and the view stretches it into
        // whatever rectangle the window has room for. Mouse positions are
        // mapped back through the same view.
        sf::View uiView;      // 1:1 with the window, for panels
        sf::View boardView;   // 0..352 stretched over the board rectangle
        float boardScale = 1.0f;
        void setCourseLayout(bool on);
        void applyViews(unsigned int width, unsigned int height, float scale);
        std::string openCourseFolderDialog();

        // Bulk PGN analysis state
        std::thread bulkAnalysisThread;
        std::atomic<bool> bulkAnalysisActive{false};
        std::atomic<bool> bulkAnalysisCancel{false};
        struct BulkProgress {
            std::size_t gameIndex = 0;
            std::size_t gameCount = 0;
            std::size_t positionIndex = 0;
            std::size_t positionCount = 0;
            std::string label;
        };
        std::mutex bulkProgressMutex;
        BulkProgress bulkProgress;
        std::string bulkOutputPath;
        std::string bulkMessage;
        bool bulkAnalysisCompleted = false;
        sf::Clock bulkMessageClock;

		// Status message
		std::string statusMessage;
		sf::Clock statusClock;

		// Engine lines for display, plus their rendered form. Turning a PV
		// into SAN runs move generation, so it is done once when the engine
		// reports rather than on every frame of the panel.
        std::vector<EngineLine> currentLines;
        std::vector<CourseTrainer::EngineReadout> currentLineText;

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
		void startBulkAnalysis(const std::string& filePath);
		std::string openFileDialog();
		std::string saveFileDialog();
        std::string openCSVFileDialog();

};

#endif /* GAME_H */
