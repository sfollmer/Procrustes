#ifndef MAINWINDOW_H_
#define MAINWINDOW_H_

#include <QMainWindow>
#include "ui_MainWindow.h"
#include "openscad.h"
#include "modcontext.h"
#include "module.h"
#include "Tree.h"
#include "memory.h"
#include <vector>
#include <QMutex>
#include "dxfdata.h"
typedef std::pair<double, double> Point2D;

typedef struct
{
    double xLoc, yLoc;
    bool insidePoly;
    bool nearEdge;
    double currentEnergy;
    double possibleEnergy[12];
} gridCell;

class MainWindow : public QMainWindow, public Ui::MainWindow
{
	Q_OBJECT

public:
	
    int offset;
    static void requestOpenFile(const QString &filename);

	QString fileName;
	class Highlighter *highlighter;

	class Preferences *prefs;

	QTimer *animate_timer;
	double tval, fps, fsteps;
    
    double cutPlaneX, cutPlaneY, cutPlaneZ, cutPlaneXRot, cutPlaneYRot, cutPlaneZRot;
    double cutPlaneCamAngleX, cutPlaneCamAngleZ, cutPlaneCamAngleY;
    double cutPlaneScreenAngle;
    double handleWidth, handleHeight, handleDepth ;
    double handleX, handleY, handleZ;
    double screw1X, screw1Y, screw1Z, screw2X, screw2Y, screw2Z, screw3X, screw3Y, screw3Z;
    double xRot, yRot, zRot;
    double binvoxTransX, binvoxTransY, binvoxTransZ, binvoxScale;
    QVector <double> cuttingPlaneMatrix;
    Transform3d cuttingPlaneMatrixRot;
    bool crossSectionMode = false;
    int crossSectionModelNumber = 0;
    class CGAL_Nef_polyhedron * crossSectionTarget;
    class CGAL_Nef_polyhedron * crossSectionEnclosure;
    class CGAL_Nef_polyhedron * crossSectionTargetInserted;
    class CGAL_Nef_polyhedron * crossSectionEncMinTarget;
    QVector<Point2D> cavityPointsTarget;
    QVector<Point2D> cavityPointsInserted;
    Transform3d transMatrixInverse;
    
    
    
    QString targetFileName, enclosureFileName, alignFileName;
    
    enum SideViews { BOTH, RIGHT, LEFT };
    SideViews viewSide = BOTH;
    

	QTimer *autoReloadTimer;
	std::string autoReloadId;
	QTimer *waitAfterReloadTimer;

	ModuleContext top_ctx;
	FileModule *root_module;      // Result of parsing
	ModuleInstantiation root_inst;    // Top level instance
	AbstractNode *absolute_root_node; // Result of tree evaluation
	AbstractNode *root_node;          // Root if the root modifier (!) is used
	Tree tree;

	shared_ptr<class CSGTerm> root_raw_term;           // Result of CSG term rendering
	shared_ptr<CSGTerm> root_norm_term;          // Normalized CSG products
	class CSGChain *root_chain;
#ifdef ENABLE_CGAL
	class CGAL_Nef_polyhedron *root_N;
	class CGALRenderer *cgalRenderer;
#endif
#ifdef ENABLE_OPENCSG
	class OpenCSGRenderer *opencsgRenderer;
#endif
	class ThrownTogetherRenderer *thrownTogetherRenderer;

	std::vector<shared_ptr<CSGTerm> > highlight_terms;
	CSGChain *highlights_chain;
	std::vector<shared_ptr<CSGTerm> > background_terms;
	CSGChain *background_chain;
	QString last_compiled_doc;

	static const int maxRecentFiles = 10;
	QAction *actionRecentFile[maxRecentFiles];

	MainWindow(const QString &filename);
	~MainWindow();

protected:
	void closeEvent(QCloseEvent *event);

private slots:
	void updatedFps();
	void updateTVal();
	void setFileName(const QString &filename);
	void setFont(const QString &family, uint size);
	void showProgress();
	void openCSGSettingsChanged();

private:
	void openFile(const QString &filename);
	void refreshDocument();
	void updateTemporalVariables();
	bool fileChangedOnDisk();
	void compileTopLevelDocument();
	void compile(bool reload, bool forcedone = false);
	void compileCSG(bool procevents);
	bool maybeSave();
	bool checkEditorModified();
	QString dumpCSGTree(AbstractNode *root);
	static void consoleOutput(const std::string &msg, void *userdata);
	void loadViewSettings();
	void loadDesignSettings();

  class QMessageBox *openglbox;

private slots:
	void actionUpdateCheck();
	void actionNew();
	void actionOpen();
	void actionOpenRecent();
	void actionOpenExample();
	void updateRecentFiles();
	void clearRecentFiles();
	void updateRecentFileActions();
	void actionSave();
	void actionSaveAs();
	void actionReload();
	void actionShowLibraryFolder();

	void instantiateRoot();
	void compileDone(bool didchange);
	void compileEnded();

private slots:
	void pasteViewportTranslation();
	void pasteViewportRotation();
	void hideEditor();
	void preferences();

private slots:
	void actionRenderCSG();
	void csgRender();
	void csgReloadRender();
#ifdef ENABLE_CGAL
	void actionRenderCGAL();
	void actionRenderCGALDone(class CGAL_Nef_polyhedron *);
	void cgalRender();
#endif
	void actionDisplayAST();
	void actionDisplayCSGTree();
	void actionDisplayCSGProducts();
	void actionExportSTLorOFF(bool stl_mode);
	void actionExportSTL();
	void actionExportOFF();
	void actionExportDXF();
	void actionExportCSG();
	void actionExportImage();
	void actionFlushCaches();
    void actionTestSeanCode();
    void setSliderVal(int val);
    void addCubeAction();
    void seanRenderCGALAction();
    void openModelFileAction();

public:
	void viewModeActionsUncheck();
	void setCurrentOutput();
	void clearCurrentOutput();

public slots:
	void actionReloadRenderCSG();
#ifdef ENABLE_OPENCSG
	void viewModeOpenCSG();
#endif
#ifdef ENABLE_CGAL
	void viewModeCGALSurface();
	void viewModeCGALGrid();
#endif
	void viewModeThrownTogether();
	void viewModeShowEdges();
	void viewModeShowAxes();
	void viewModeShowCrosshairs();
	void viewModeAnimate();
	void viewAngleTop();
	void viewAngleBottom();
	void viewAngleLeft();
	void viewAngleRight();
	void viewAngleFront();
	void viewAngleBack();
	void viewAngleDiagonal();
	void viewCenter();
	void viewPerspective();
	void viewOrthogonal();
	void hideConsole();
	void animateUpdateDocChanged();
	void animateUpdate();
	void dragEnterEvent(QDragEnterEvent *event);
	void dropEvent(QDropEvent *event);
	void helpAbout();
	void helpHomepage();
	void helpManual();
	void helpLibrary();
	void quit();
	void checkAutoReload();
	void waitAfterReload();
	void autoReloadSet(bool);
	// Procrustes begin
  void cuttingPlaneAction();
	void insertionDirectionAction();
	void demoButtonAction();
	void targetButtonOpenAction();
	void enclosureButtonAction();
	void alignButtonAction();
	void cuttingPlaneButtonAction();
	void insertionButtonAction();
	void exportSTLSeanButtonAction();
	// Procrustes end
	void loadOriginalFiles();
	void loadInsertedFiles();
	void readBinvoxScaleTranslate(std::string filespec);
	void addCuttingPlaneAfter();
	void crossSection();
	void computeScrewPositionsAction();
	void crossSectionOutLines();
	void crossSectionModel(std::string modelFileName);
	void crossSectionModelBinvox();
	QVector<Point2D> findCloseCavityPoints(class CGAL_Nef_polyhedron *target,class CGAL_Nef_polyhedron *enclosure);
	int pnpoly( DxfData *dd, double testx, double testy);
	void findScrewPositions(DxfData *dd);
	Vector2d picRandomPointInPolygon(DxfData *dd, int minX, int minY, int yDist, int xDist);
	double distanceToPoint(double x1, double y1, double x2, double y2 );
	double distToLineSegment(double x1, double y1, double x2, double y2, double x3, double y3);
	bool testIfCloseToPolygonEdge(DxfData *dd, double testx, double testy, double minDist);
	int pickRandomGoodGridCell(gridCell* grid, int gridSize);
	void addScrewHole(double xPos, double yPos);
	void loadInsertedFilesNoRender();
	void addCuttingPlaneAfterNoRender();
	void testDrawScrewPos(double xPos, double yPos);
	void testDrawScrewPosAssembly();

private:
	static void report_func(const class AbstractNode*, void *vp, int mark);

	char const * afterCompileSlot;
	bool procevents;
	
	class ProgressWidget *progresswidget;
	class CGALWorker *cgalworker;
	QMutex consolemutex;
};

class GuiLocker
{
public:
	GuiLocker() {
		gui_locked++;
	}
	~GuiLocker() {
		gui_locked--;
	}
	static bool isLocked() { return gui_locked > 0; }
	static void lock() { gui_locked++; }
	static void unlock() { gui_locked--; }

private:
	static unsigned int gui_locked;
};

#endif
