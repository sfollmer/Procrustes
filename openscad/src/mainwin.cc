/*
 *  OpenSCAD (www.openscad.org)
 *  Copyright (C) 2009-2011 Clifford Wolf <clifford@clifford.at> and
 *                          Marius Kintel <marius@kintel.net>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  As a special exception, you have permission to link this program
 *  with the CGAL library and distribute executables, as long as you
 *  follow the requirements of the GNU GPL in regard to all of the
 *  software in the executable aside from CGAL.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include "PolySetCache.h"
#include "ModuleCache.h"
#include "MainWindow.h"
#include "openscad.h" // examplesdir
#include "parsersettings.h" 
#include "Preferences.h"
#include "printutils.h"
#include "node.h"
#include "polyset.h"
#include "primitives.h"
#include "transformnode.h"
#include "linalg.h"
#include "csgterm.h"
#include "highlighter.h"
#include "export.h"
#include "builtin.h"
#include "progress.h"
#include "dxfdim.h"
#include "AboutDialog.h"
#ifdef ENABLE_OPENCSG
#include "CSGTermEvaluator.h"
#include "OpenCSGRenderer.h"
#include <opencsg.h>
#endif
#include "ProgressWidget.h"
#include "ThrownTogetherRenderer.h"
#include "csgtermnormalizer.h"
#include "QGLView.h"
#include "AutoUpdater.h"
#ifdef Q_OS_MAC
#include "CocoaUtils.h"
#endif
#include "PlatformUtils.h"
#include "CsgInfo.h"


#include <QMenu>
#include <QTime>
#include <QMenuBar>
#include <QSplitter>
#include <QFileDialog>
#include <QApplication>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QFileInfo>
#include <QTextStream>
#include <QStatusBar>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QTimer>
#include <QMessageBox>
#include <QDesktopServices>
#include <QSettings>
#include <QProgressDialog>
#include <QMutexLocker>

#include <fstream>

#include <algorithm>
#include <boost/version.hpp>
#include <boost/foreach.hpp>
#include <boost/version.hpp>
#include <sys/stat.h>

#ifdef ENABLE_CGAL

#include "CGALCache.h"
#include "CGALEvaluator.h"
#include "PolySetCGALEvaluator.h"
#include "CGALRenderer.h"
#include "CGAL_Nef_polyhedron.h"
#include "cgal.h"
#include "cgalworker.h"

#else

#include "PolySetEvaluator.h"

#endif // ENABLE_CGAL

#ifndef OPENCSG_VERSION_STRING
#define OPENCSG_VERSION_STRING "unknown, <1.3.2"
#endif

#include "boosty.h"

extern QString examplesdir;

// Global application state
unsigned int GuiLocker::gui_locked = 0;

#define QUOTE(x__) # x__
#define QUOTED(x__) QUOTE(x__)

static char helptitle[] =
	"OpenSCAD " QUOTED(OPENSCAD_VERSION)
#ifdef OPENSCAD_COMMIT
	" (git " QUOTED(OPENSCAD_COMMIT) ")"
#endif
	"\nhttp://www.openscad.org\n\n";
static char copyrighttext[] =
	"Copyright (C) 2009-2013 The OpenSCAD Developers\n"
	"\n"
	"This program is free software; you can redistribute it and/or modify "
	"it under the terms of the GNU General Public License as published by "
	"the Free Software Foundation; either version 2 of the License, or "
	"(at your option) any later version.";

static void
settings_setValueList(const QString &key,const QList<int> &list)
{
	QSettings settings;
	settings.beginWriteArray(key);
	for (int i=0;i<list.size(); ++i) {
		settings.setArrayIndex(i);
		settings.setValue("entry",list[i]);
	}
	settings.endArray();
}

QList<int>
settings_valueList(const QString &key, const QList<int> &defaultList = QList<int>())
{
	QSettings settings;
	QList<int> result;
	if (settings.contains(key+"/size")){
		int length = settings.beginReadArray(key);
		for (int i = 0; i < length; ++i) {
			settings.setArrayIndex(i);
			result += settings.value("entry").toInt();
		}
		settings.endArray();
		return result;
	} else {
		return defaultList;
	}

}

MainWindow::MainWindow(const QString &filename)
	: root_inst("group"), progresswidget(NULL)
{
	setupUi(this);

#ifdef ENABLE_CGAL
	this->cgalworker = new CGALWorker();
	connect(this->cgalworker, SIGNAL(done(CGAL_Nef_polyhedron *)), 
					this, SLOT(actionRenderCGALDone(CGAL_Nef_polyhedron *)));
#endif

	top_ctx.registerBuiltin();

	this->openglbox = NULL;
	root_module = NULL;
	absolute_root_node = NULL;
	this->root_chain = NULL;
#ifdef ENABLE_CGAL
	this->root_N = NULL;
	this->cgalRenderer = NULL;
#endif
#ifdef ENABLE_OPENCSG
	this->opencsgRenderer = NULL;
#endif
	this->thrownTogetherRenderer = NULL;

	highlights_chain = NULL;
	background_chain = NULL;
	root_node = NULL;

	tval = 0;
	fps = 0;
	fsteps = 1;
    offset = 2;
    
	highlighter = new Highlighter(editor->document());
	editor->setTabStopWidth(30);
	editor->setLineWrapping(true); // Not designable

	this->qglview->statusLabel = new QLabel(this);
	statusBar()->addWidget(this->qglview->statusLabel);

	animate_timer = new QTimer(this);
	connect(animate_timer, SIGNAL(timeout()), this, SLOT(updateTVal()));

	autoReloadTimer = new QTimer(this);
	autoReloadTimer->setSingleShot(false);
	autoReloadTimer->setInterval(200);
	connect(autoReloadTimer, SIGNAL(timeout()), this, SLOT(checkAutoReload()));

	waitAfterReloadTimer = new QTimer(this);
	waitAfterReloadTimer->setSingleShot(true);
	waitAfterReloadTimer->setInterval(200);
	connect(waitAfterReloadTimer, SIGNAL(timeout()), this, SLOT(waitAfterReload()));

	connect(this->e_tval, SIGNAL(textChanged(QString)), this, SLOT(actionRenderCSG()));
	connect(this->e_fps, SIGNAL(textChanged(QString)), this, SLOT(updatedFps()));

	animate_panel->hide();

	// Application menu
#ifdef DEBUG
	this->appActionUpdateCheck->setEnabled(false);
#else
#ifdef Q_OS_MAC
	this->appActionUpdateCheck->setMenuRole(QAction::ApplicationSpecificRole);
	this->appActionUpdateCheck->setEnabled(true);
	connect(this->appActionUpdateCheck, SIGNAL(triggered()), this, SLOT(actionUpdateCheck()));
#endif
#endif
	// File menu
	connect(this->fileActionNew, SIGNAL(triggered()), this, SLOT(actionNew()));
	connect(this->fileActionOpen, SIGNAL(triggered()), this, SLOT(actionOpen()));
	connect(this->fileActionSave, SIGNAL(triggered()), this, SLOT(actionSave()));
	connect(this->fileActionSaveAs, SIGNAL(triggered()), this, SLOT(actionSaveAs()));
	connect(this->fileActionReload, SIGNAL(triggered()), this, SLOT(actionReload()));
	connect(this->fileActionQuit, SIGNAL(triggered()), this, SLOT(quit()));
	connect(this->fileShowLibraryFolder, SIGNAL(triggered()), this, SLOT(actionShowLibraryFolder()));
#ifndef __APPLE__
	QList<QKeySequence> shortcuts = this->fileActionSave->shortcuts();
	shortcuts.push_back(QKeySequence(Qt::Key_F2));
	this->fileActionSave->setShortcuts(shortcuts);
	shortcuts = this->fileActionReload->shortcuts();
	shortcuts.push_back(QKeySequence(Qt::Key_F3));
	this->fileActionReload->setShortcuts(shortcuts);
#endif
	// Open Recent
	for (int i = 0;i<maxRecentFiles; i++) {
		this->actionRecentFile[i] = new QAction(this);
		this->actionRecentFile[i]->setVisible(false);
		this->menuOpenRecent->addAction(this->actionRecentFile[i]);
		connect(this->actionRecentFile[i], SIGNAL(triggered()),
						this, SLOT(actionOpenRecent()));
	}
	this->menuOpenRecent->addSeparator();
	this->menuOpenRecent->addAction(this->fileActionClearRecent);
	connect(this->fileActionClearRecent, SIGNAL(triggered()),
					this, SLOT(clearRecentFiles()));

	if (!examplesdir.isEmpty()) {
		bool found_example = false;
		QStringList examples = QDir(examplesdir).entryList(QStringList("*.scad"), 
		QDir::Files | QDir::Readable, QDir::Name);
		foreach (const QString &ex, examples) {
			this->menuExamples->addAction(ex, this, SLOT(actionOpenExample()));
			found_example = true;
		}
		if (!found_example) {
			delete this->menuExamples;
			this->menuExamples = NULL;
		}
	} else {
		delete this->menuExamples;
		this->menuExamples = NULL;
	}

	// Edit menu
	connect(this->editActionUndo, SIGNAL(triggered()), editor, SLOT(undo()));
	connect(this->editActionRedo, SIGNAL(triggered()), editor, SLOT(redo()));
	connect(this->editActionCut, SIGNAL(triggered()), editor, SLOT(cut()));
	connect(this->editActionCopy, SIGNAL(triggered()), editor, SLOT(copy()));
	connect(this->editActionPaste, SIGNAL(triggered()), editor, SLOT(paste()));
	connect(this->editActionIndent, SIGNAL(triggered()), editor, SLOT(indentSelection()));
	connect(this->editActionUnindent, SIGNAL(triggered()), editor, SLOT(unindentSelection()));
	connect(this->editActionComment, SIGNAL(triggered()), editor, SLOT(commentSelection()));
	connect(this->editActionUncomment, SIGNAL(triggered()), editor, SLOT(uncommentSelection()));
	connect(this->editActionPasteVPT, SIGNAL(triggered()), this, SLOT(pasteViewportTranslation()));
	connect(this->editActionPasteVPR, SIGNAL(triggered()), this, SLOT(pasteViewportRotation()));
	connect(this->editActionZoomIn, SIGNAL(triggered()), editor, SLOT(zoomIn()));
	connect(this->editActionZoomOut, SIGNAL(triggered()), editor, SLOT(zoomOut()));
	connect(this->editActionHide, SIGNAL(triggered()), this, SLOT(hideEditor()));
	connect(this->editActionPreferences, SIGNAL(triggered()), this, SLOT(preferences()));

	// Design menu
	connect(this->designActionAutoReload, SIGNAL(toggled(bool)), this, SLOT(autoReloadSet(bool)));
	connect(this->designActionReloadAndCompile, SIGNAL(triggered()), this, SLOT(actionReloadRenderCSG()));
	connect(this->designActionCompile, SIGNAL(triggered()), this, SLOT(actionRenderCSG()));
#ifdef ENABLE_CGAL
	connect(this->designActionCompileAndRender, SIGNAL(triggered()), this, SLOT(actionRenderCGAL()));
#else
	this->designActionCompileAndRender->setVisible(false);
#endif
	connect(this->designActionDisplayAST, SIGNAL(triggered()), this, SLOT(actionDisplayAST()));
	connect(this->designActionDisplayCSGTree, SIGNAL(triggered()), this, SLOT(actionDisplayCSGTree()));
	connect(this->designActionDisplayCSGProducts, SIGNAL(triggered()), this, SLOT(actionDisplayCSGProducts()));
	connect(this->designActionExportSTL, SIGNAL(triggered()), this, SLOT(actionExportSTL()));
	connect(this->designActionExportOFF, SIGNAL(triggered()), this, SLOT(actionExportOFF()));
	connect(this->designActionExportDXF, SIGNAL(triggered()), this, SLOT(actionExportDXF()));
	connect(this->designActionExportCSG, SIGNAL(triggered()), this, SLOT(actionExportCSG()));
	connect(this->designActionExportImage, SIGNAL(triggered()), this, SLOT(actionExportImage()));
	connect(this->designActionFlushCaches, SIGNAL(triggered()), this, SLOT(actionFlushCaches()));
    connect(this->designActionTestSeanCode, SIGNAL(triggered()), this, SLOT(actionTestSeanCode()));
    

	// View menu
#ifndef ENABLE_OPENCSG
	this->viewActionOpenCSG->setVisible(false);
#else
	connect(this->viewActionOpenCSG, SIGNAL(triggered()), this, SLOT(viewModeOpenCSG()));
	if (!this->qglview->hasOpenCSGSupport()) {
		this->viewActionOpenCSG->setEnabled(false);
	}
#endif

#ifdef ENABLE_CGAL
	connect(this->viewActionCGALSurfaces, SIGNAL(triggered()), this, SLOT(viewModeCGALSurface()));
	connect(this->viewActionCGALGrid, SIGNAL(triggered()), this, SLOT(viewModeCGALGrid()));
#else
	this->viewActionCGALSurfaces->setVisible(false);
	this->viewActionCGALGrid->setVisible(false);
#endif
	connect(this->viewActionThrownTogether, SIGNAL(triggered()), this, SLOT(viewModeThrownTogether()));
	connect(this->viewActionShowEdges, SIGNAL(triggered()), this, SLOT(viewModeShowEdges()));
	connect(this->viewActionShowAxes, SIGNAL(triggered()), this, SLOT(viewModeShowAxes()));
	connect(this->viewActionShowCrosshairs, SIGNAL(triggered()), this, SLOT(viewModeShowCrosshairs()));
	connect(this->viewActionAnimate, SIGNAL(triggered()), this, SLOT(viewModeAnimate()));
	connect(this->viewActionTop, SIGNAL(triggered()), this, SLOT(viewAngleTop()));
	connect(this->viewActionBottom, SIGNAL(triggered()), this, SLOT(viewAngleBottom()));
	connect(this->viewActionLeft, SIGNAL(triggered()), this, SLOT(viewAngleLeft()));
	connect(this->viewActionRight, SIGNAL(triggered()), this, SLOT(viewAngleRight()));
	connect(this->viewActionFront, SIGNAL(triggered()), this, SLOT(viewAngleFront()));
	connect(this->viewActionBack, SIGNAL(triggered()), this, SLOT(viewAngleBack()));
	connect(this->viewActionDiagonal, SIGNAL(triggered()), this, SLOT(viewAngleDiagonal()));
	connect(this->viewActionCenter, SIGNAL(triggered()), this, SLOT(viewCenter()));
	connect(this->viewActionPerspective, SIGNAL(triggered()), this, SLOT(viewPerspective()));
	connect(this->viewActionOrthogonal, SIGNAL(triggered()), this, SLOT(viewOrthogonal()));
	connect(this->viewActionHide, SIGNAL(triggered()), this, SLOT(hideConsole()));

	// Help menu
	connect(this->helpActionAbout, SIGNAL(triggered()), this, SLOT(helpAbout()));
	connect(this->helpActionHomepage, SIGNAL(triggered()), this, SLOT(helpHomepage()));
	connect(this->helpActionManual, SIGNAL(triggered()), this, SLOT(helpManual()));
	connect(this->helpActionLibraryInfo, SIGNAL(triggered()), this, SLOT(helpLibrary()));

    // Gui panel
    connect(this->horizontalSlider1, SIGNAL(valueChanged(int)), this, SLOT(setSliderVal(int)));
    connect(this->addCube, SIGNAL(released()), this, SLOT(openModelFileAction()));
    connect(this->seanRenderCGAL, SIGNAL(released()), this, SLOT(seanRenderCGALAction()));
		connect(this->demoButton, SIGNAL(released()), this, SLOT(demoButtonAction()));
    connect(this->targetButton, SIGNAL(released()), this, SLOT(targetButtonOpenAction()));
    connect(this->enclosureButton, SIGNAL(released()), this, SLOT(enclosureButtonAction()));
    connect(this->alignButton, SIGNAL(released()), this, SLOT(alignButtonAction()));
    connect(this->cuttingPlaneButton, SIGNAL(released()), this, SLOT(cuttingPlaneButtonAction()));
    connect(this->insertionButton, SIGNAL(released()), this, SLOT(insertionButtonAction()));
    connect(this->exportSTLSean, SIGNAL(released()), this, SLOT(exportSTLSeanButtonAction()));
    connect(this->computeScrewPositions, SIGNAL(released()), this, SLOT(computeScrewPositionsAction()));
    
	setCurrentOutput();

	PRINT(helptitle);
	PRINT(copyrighttext);
	PRINT("");

	if (!filename.isEmpty()) {
		openFile(filename);
	} else {
		setFileName("");
	}
	updateRecentFileActions();

	connect(editor->document(), SIGNAL(contentsChanged()), this, SLOT(animateUpdateDocChanged()));
	connect(editor->document(), SIGNAL(modificationChanged(bool)), this, SLOT(setWindowModified(bool)));
	connect(editor->document(), SIGNAL(modificationChanged(bool)), fileActionSave, SLOT(setEnabled(bool)));
	connect(this->qglview, SIGNAL(doAnimateUpdate()), this, SLOT(animateUpdate()));

	connect(Preferences::inst(), SIGNAL(requestRedraw()), this->qglview, SLOT(updateGL()));
	connect(Preferences::inst(), SIGNAL(fontChanged(const QString&,uint)), 
					this, SLOT(setFont(const QString&,uint)));
	connect(Preferences::inst(), SIGNAL(openCSGSettingsChanged()),
					this, SLOT(openCSGSettingsChanged()));
	Preferences::inst()->apply();

	// make sure it looks nice..
	QSettings settings;
	resize(settings.value("window/size", QSize(800, 600)).toSize());
	move(settings.value("window/position", QPoint(0, 0)).toPoint());
	QList<int> s1sizes = settings_valueList("window/splitter1sizes",QList<int>()<<400<<400);
	QList<int> s2sizes = settings_valueList("window/splitter2sizes",QList<int>()<<400<<200);
	splitter1->setSizes(s1sizes);
	splitter2->setSizes(s2sizes);

	// display this window and check for OpenGL 2.0 (OpenCSG) support
	viewModeThrownTogether();
	show();
    
    connect(this->qglview, SIGNAL(drewLine()), this, SLOT(cuttingPlaneAction()));
    connect(this->qglview, SIGNAL(drewInsertionLine()), this, SLOT(insertionDirectionAction()));

#ifdef ENABLE_OPENCSG
	viewModeOpenCSG();
#else
	viewModeThrownTogether();
#endif
	loadViewSettings();
	loadDesignSettings();

	setAcceptDrops(true);
	clearCurrentOutput();
}

void
MainWindow::loadViewSettings(){
	QSettings settings;
	if (settings.value("view/showEdges").toBool()) {
		viewActionShowEdges->setChecked(true);
		viewModeShowEdges();
	}
	if (settings.value("view/showAxes").toBool()) {
		viewActionShowAxes->setChecked(true);
		viewModeShowAxes();
	}
	if (settings.value("view/showCrosshairs").toBool()) {
		viewActionShowCrosshairs->setChecked(true);
		viewModeShowCrosshairs();
	}
	if (settings.value("view/orthogonalProjection").toBool()) {
		viewOrthogonal();
	} else {
		viewPerspective();
	}
	if (settings.value("view/hideConsole").toBool()) {
		viewActionHide->setChecked(true);
		hideConsole();
	}
	if (settings.value("view/hideEditor").toBool()) {
		editActionHide->setChecked(true);
		hideEditor();
	}
}

void
MainWindow::loadDesignSettings()
{
	QSettings settings;
	if (settings.value("design/autoReload").toBool()) {
		designActionAutoReload->setChecked(true);
	}
	uint polySetCacheSize = Preferences::inst()->getValue("advanced/polysetCacheSize").toUInt();
	PolySetCache::instance()->setMaxSize(polySetCacheSize);
#ifdef ENABLE_CGAL
	uint cgalCacheSize = Preferences::inst()->getValue("advanced/cgalCacheSize").toUInt();
	CGALCache::instance()->setMaxSize(cgalCacheSize);
#endif
}

MainWindow::~MainWindow()
{
	if (root_module) delete root_module;
	if (root_node) delete root_node;
#ifdef ENABLE_CGAL
	if (this->root_N) delete this->root_N;
	delete this->cgalRenderer;
#endif
#ifdef ENABLE_OPENCSG
	delete this->opencsgRenderer;
#endif
}

void MainWindow::showProgress()
{
	this->statusBar()->addPermanentWidget(qobject_cast<ProgressWidget*>(sender()));
}

void MainWindow::report_func(const class AbstractNode*, void *vp, int mark)
{
	MainWindow *thisp = static_cast<MainWindow*>(vp);
	int v = (int)((mark*1000.0) / progress_report_count);
	int permille = v < 1000 ? v : 999; 
	if (permille > thisp->progresswidget->value()) {
		QMetaObject::invokeMethod(thisp->progresswidget, "setValue", Qt::QueuedConnection,
															Q_ARG(int, permille));
		QApplication::processEvents();
	}

	// FIXME: Check if cancel was requested by e.g. Application quit
	if (thisp->progresswidget->wasCanceled()) throw ProgressCancelException();
}

/*!
	Requests to open a file from an external event, e.g. by double-clicking a filename.
 */
#ifdef ENABLE_MDI
void MainWindow::requestOpenFile(const QString &filename)
{
	new MainWindow(filename);
}
#else
void MainWindow::requestOpenFile(const QString &)
{
}
#endif

void
MainWindow::openFile(const QString &new_filename)
{
	QString actual_filename = new_filename;
	QFileInfo fi(new_filename);
	if (fi.suffix().toLower().contains(QRegExp("^(stl|off|dxf)$"))) {
		actual_filename = QString();
	}
#ifdef ENABLE_MDI
	if (!editor->toPlainText().isEmpty()) {
		new MainWindow(actual_filename);
		clearCurrentOutput();
		return;
	}
#endif
	setFileName(actual_filename);

	fileChangedOnDisk(); // force cached autoReloadId to update
	refreshDocument();
	updateRecentFiles();
	if (actual_filename.isEmpty()) {
		this->editor->setPlainText(QString("import(\"%1\");\n").arg(new_filename));
	}
}

void
MainWindow::setFileName(const QString &filename)
{
	if (filename.isEmpty()) {
		this->fileName.clear();
		this->top_ctx.setDocumentPath(currentdir);
		setWindowTitle("OpenSCAD - New Document[*]");
	}
	else {
		QFileInfo fileinfo(filename);
		setWindowTitle("OpenSCAD - " + fileinfo.fileName() + "[*]");

		// Check that the canonical file path exists - only update recent files
		// if it does. Should prevent empty list items on initial open etc.
		QString infoFileName = fileinfo.absoluteFilePath();
		if (!infoFileName.isEmpty()) {
			this->fileName = infoFileName;
		} else {
			this->fileName = fileinfo.fileName();
		}
		
		this->top_ctx.setDocumentPath(fileinfo.dir().absolutePath().toLocal8Bit().constData());
		QDir::setCurrent(fileinfo.dir().absolutePath());
	}

}

void MainWindow::updateRecentFiles()
{
	// Check that the canonical file path exists - only update recent files
	// if it does. Should prevent empty list items on initial open etc.
	QFileInfo fileinfo(this->fileName);
	QString infoFileName = fileinfo.absoluteFilePath();
	QSettings settings; // already set up properly via main.cpp
	QStringList files = settings.value("recentFileList").toStringList();
	files.removeAll(infoFileName);
	files.prepend(infoFileName);
	while (files.size() > maxRecentFiles) files.removeLast();
	settings.setValue("recentFileList", files);

	foreach(QWidget *widget, QApplication::topLevelWidgets()) {
		MainWindow *mainWin = qobject_cast<MainWindow *>(widget);
		if (mainWin) {
			mainWin->updateRecentFileActions();
		}
	}
}


void MainWindow::updatedFps()
{
	bool fps_ok;
	double fps = this->e_fps->text().toDouble(&fps_ok);
	animate_timer->stop();
	if (fps_ok && fps > 0) {
		animate_timer->setSingleShot(false);
		animate_timer->setInterval(int(1000 / this->e_fps->text().toDouble()));
		animate_timer->start();
	}
}

void MainWindow::updateTVal()
{
	bool fps_ok;
	double fps = this->e_fps->text().toDouble(&fps_ok);
	if (fps_ok) {
		if (fps <= 0) {
			actionRenderCSG();
		} else {
			double s = this->e_fsteps->text().toDouble();
			double t = this->e_tval->text().toDouble() + 1/s;
			QString txt;
			txt.sprintf("%.5f", t >= 1.0 ? 0.0 : t);
			this->e_tval->setText(txt);
		}
	}
}

void MainWindow::refreshDocument()
{
	setCurrentOutput();
	if (!this->fileName.isEmpty()) {
		QFile file(this->fileName);
		if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
			PRINTB("Failed to open file %s: %s", 
						 this->fileName.toLocal8Bit().constData() % file.errorString().toLocal8Bit().constData());
		}
		else {
			QTextStream reader(&file);
			reader.setCodec("UTF-8");
			QString text = reader.readAll();
			PRINTB("Loaded design '%s'.", this->fileName.toLocal8Bit().constData());
			editor->setPlainText(text);
		}
	}
	setCurrentOutput();
}

/*!
	compiles the design. Calls compileDone() if anything was compiled
*/
void MainWindow::compile(bool reload, bool forcedone)
{
	bool shouldcompiletoplevel = false;
	bool didcompile = false;

	// Reload checks the timestamp of the toplevel file and refreshes if necessary,
	if (reload) {
		// Refresh files if it has changed on disk
		if (fileChangedOnDisk() && checkEditorModified()) {
			shouldcompiletoplevel = true;
			refreshDocument();
		}
		// If the file hasn't changed, we might still need to compile it
		// if we haven't yet compiled the current text.
		else {
			QString current_doc = editor->toPlainText();
			if (current_doc != last_compiled_doc)	shouldcompiletoplevel = true;
		}
	}
	else {
		shouldcompiletoplevel = true;
	}

	if (!shouldcompiletoplevel && this->root_module && this->root_module->includesChanged()) {
		shouldcompiletoplevel = true;
	}

	if (shouldcompiletoplevel) {
		console->clear();
		compileTopLevelDocument();
		didcompile = true;
	}

	if (this->root_module) {
		if (this->root_module->handleDependencies()) {
			PRINTB("Module cache size: %d modules", ModuleCache::instance()->size());
			didcompile = true;
		}
	}

	// If we're auto-reloading, listen for a cascade of changes by starting a timer
	// if something changed _and_ there are any external dependencies
	if (reload && didcompile && this->root_module) {
		if (this->root_module->hasIncludes() ||
				this->root_module->usesLibraries()) {
			this->waitAfterReloadTimer->start();
			return;
		}
	}
	compileDone(didcompile | forcedone);
}

void MainWindow::waitAfterReload()
{
	if (this->root_module->handleDependencies()) {
		this->waitAfterReloadTimer->start();
		return;
	}
	else {
		compile(true, true); // In case file itself or top-level includes changed during dependency updates
	}
}

void MainWindow::compileDone(bool didchange)
{
	const char *callslot;
	if (didchange) {
		instantiateRoot();
		callslot = afterCompileSlot;
	}
	else {
		callslot = "compileEnded";
	}

	this->procevents = false;
	QMetaObject::invokeMethod(this, callslot);
}

void MainWindow::compileEnded()
{
	clearCurrentOutput();
	GuiLocker::unlock();
	if (designActionAutoReload->isChecked()) autoReloadTimer->start();
}

void MainWindow::instantiateRoot()
{
	// Go on and instantiate root_node, then call the continuation slot

  // Invalidate renderers before we kill the CSG tree
	this->qglview->setRenderer(NULL);
	delete this->opencsgRenderer;
	this->opencsgRenderer = NULL;
	delete this->thrownTogetherRenderer;
	this->thrownTogetherRenderer = NULL;

	// Remove previous CSG tree
	delete this->absolute_root_node;
	this->absolute_root_node = NULL;

	this->root_raw_term.reset();
	this->root_norm_term.reset();

	delete this->root_chain;
	this->root_chain = NULL;

	this->highlight_terms.clear();
	delete this->highlights_chain;
	this->highlights_chain = NULL;

	this->background_terms.clear();
	delete this->background_chain;
	this->background_chain = NULL;

	this->root_node = NULL;
	this->tree.setRoot(NULL);

	if (this->root_module) {
		// Evaluate CSG tree
		PRINT("Compiling design (CSG Tree generation)...");
		if (this->procevents) QApplication::processEvents();
		
		AbstractNode::resetIndexCounter();

		// split these two lines - gcc 4.7 bug
		ModuleInstantiation mi = ModuleInstantiation( "group" );
		this->root_inst = mi; 

		this->absolute_root_node = this->root_module->instantiate(&top_ctx, &this->root_inst, NULL);
		
		if (this->absolute_root_node) {
			// Do we have an explicit root node (! modifier)?
			if (!(this->root_node = find_root_tag(this->absolute_root_node))) {
				this->root_node = this->absolute_root_node;
			}
			// FIXME: Consider giving away ownership of root_node to the Tree, or use reference counted pointers
			this->tree.setRoot(this->root_node);
			// Dump the tree (to initialize caches).
			// FIXME: We shouldn't really need to do this explicitly..
			this->tree.getString(*this->root_node);
		}
	}

	if (!this->root_node) {
		if (parser_error_pos < 0) {
			PRINT("ERROR: Compilation failed! (no top level object found)");
		} else {
			PRINT("ERROR: Compilation failed!");
		}
		if (this->procevents) QApplication::processEvents();
	}
}

/*!
	Generates CSG tree for OpenCSG evaluation.
	Assumes that the design has been parsed and evaluated (this->root_node is set)
*/
void MainWindow::compileCSG(bool procevents)
{
	assert(this->root_node);
	PRINT("Compiling design (CSG Products generation)...");
	if (procevents) QApplication::processEvents();

	// Main CSG evaluation
	QTime t;
	t.start();

	this->progresswidget = new ProgressWidget(this);
	connect(this->progresswidget, SIGNAL(requestShow()), this, SLOT(showProgress()));

	progress_report_prep(this->root_node, report_func, this);
	try {
#ifdef ENABLE_CGAL
		CGALEvaluator cgalevaluator(this->tree);
		PolySetCGALEvaluator psevaluator(cgalevaluator);
#else
		PolySetEvaluator psevaluator(this->tree);
#endif
		CSGTermEvaluator csgrenderer(this->tree, &psevaluator);
		if (procevents) QApplication::processEvents();
		this->root_raw_term = csgrenderer.evaluateCSGTerm(*root_node, highlight_terms, background_terms);
		if (!root_raw_term) {
			PRINT("ERROR: CSG generation failed! (no top level object found)");
		}
		PolySetCache::instance()->print();
#ifdef ENABLE_CGAL
		CGALCache::instance()->print();
#endif
		if (procevents) QApplication::processEvents();
	}
	catch (const ProgressCancelException &e) {
		PRINT("CSG generation cancelled.");
	}
	progress_report_fin();
	this->statusBar()->removeWidget(this->progresswidget);
	delete this->progresswidget;
	this->progresswidget = NULL;

	if (root_raw_term) {
		PRINT("Compiling design (CSG Products normalization)...");
		if (procevents) QApplication::processEvents();
		
		size_t normalizelimit = 2 * Preferences::inst()->getValue("advanced/openCSGLimit").toUInt();
		CSGTermNormalizer normalizer(normalizelimit);
		this->root_norm_term = normalizer.normalize(this->root_raw_term);
		if (this->root_norm_term) {
			this->root_chain = new CSGChain();
			this->root_chain->import(this->root_norm_term);
		}
		else {
			this->root_chain = NULL;
			PRINT("WARNING: CSG normalization resulted in an empty tree");
			if (procevents) QApplication::processEvents();
		}
		
		if (highlight_terms.size() > 0)
		{
			PRINTB("Compiling highlights (%d CSG Trees)...", highlight_terms.size());
			if (procevents) QApplication::processEvents();
			
			highlights_chain = new CSGChain();
			for (unsigned int i = 0; i < highlight_terms.size(); i++) {
				highlight_terms[i] = normalizer.normalize(highlight_terms[i]);
				highlights_chain->import(highlight_terms[i]);
			}
		}
		
		if (background_terms.size() > 0)
		{
			PRINTB("Compiling background (%d CSG Trees)...", background_terms.size());
			if (procevents) QApplication::processEvents();
			
			background_chain = new CSGChain();
			for (unsigned int i = 0; i < background_terms.size(); i++) {
				background_terms[i] = normalizer.normalize(background_terms[i]);
				background_chain->import(background_terms[i]);
			}
		}

		if (this->root_chain && 
				(this->root_chain->objects.size() > 
				 Preferences::inst()->getValue("advanced/openCSGLimit").toUInt())) {
			PRINTB("WARNING: Normalized tree has %d elements!", this->root_chain->objects.size());
			PRINT("WARNING: OpenCSG rendering has been disabled.");
		}
		else {
			PRINTB("Normalized CSG tree has %d elements", 
						 (this->root_chain ? this->root_chain->objects.size() : 0));
			this->opencsgRenderer = new OpenCSGRenderer(this->root_chain, 
																									this->highlights_chain, 
																									this->background_chain, 
																									this->qglview->shaderinfo);
		}
		this->thrownTogetherRenderer = new ThrownTogetherRenderer(this->root_chain, 
																															this->highlights_chain, 
																															this->background_chain);
		PRINT("CSG generation finished.");
		int s = t.elapsed() / 1000;
		PRINTB("Total rendering time: %d hours, %d minutes, %d seconds", (s / (60*60)) % ((s / 60) % 60) % (s % 60));
        PRINTB("Raw Time %d", t.elapsed());
		if (procevents) QApplication::processEvents();
	}
}

void MainWindow::actionUpdateCheck()
{
	if (AutoUpdater *updater =AutoUpdater::updater()) {
		updater->checkForUpdates();
	}
}

void MainWindow::actionNew()
{
#ifdef ENABLE_MDI
	new MainWindow(QString());
#else
	if (!maybeSave())
		return;

	setFileName("");
	editor->setPlainText("");
#endif
}

void MainWindow::actionOpen()
{
	QString new_filename = QFileDialog::getOpenFileName(this, "Open File", "",
																											"OpenSCAD Designs (*.scad *.csg)");
#ifdef ENABLE_MDI
	if (!new_filename.isEmpty()) {
		new MainWindow(new_filename);
	}
#else
	if (!new_filename.isEmpty()) {
		if (!maybeSave())
			return;
		
		setCurrentOutput();
		openFile(new_filename);
		clearCurrentOutput();
	}
#endif
}

void MainWindow::actionOpenRecent()
{
	QAction *action = qobject_cast<QAction *>(sender());

#ifdef ENABLE_MDI
	new MainWindow(action->data().toString());
#else
	if (!maybeSave())
		return;

	if (action) {
		openFile(action->data().toString());
	}
#endif
}

void MainWindow::clearRecentFiles()
{
	QSettings settings; // already set up properly via main.cpp
	QStringList files;
	settings.setValue("recentFileList", files);

	updateRecentFileActions();
}

void MainWindow::updateRecentFileActions()
{
	QSettings settings; // set up project and program properly in main.cpp
	QStringList files = settings.value("recentFileList").toStringList();

	int originalNumRecentFiles = files.size();

	// Remove any duplicate or empty entries from the list
#if (QT_VERSION >= QT_VERSION_CHECK(4, 5, 0))
	files.removeDuplicates();
#endif
	files.removeAll(QString());
	// Now remove any entries which do not exist
	for(int i = files.size()-1; i >= 0; --i) {
		QFileInfo fileInfo(files[i]);
		if (!QFile(fileInfo.absoluteFilePath()).exists())
			files.removeAt(i);
	}

	int numRecentFiles = qMin(files.size(),
														static_cast<int>(maxRecentFiles));

	for (int i = 0; i < numRecentFiles; ++i) {
		this->actionRecentFile[i]->setText(QFileInfo(files[i]).fileName());
		this->actionRecentFile[i]->setData(files[i]);
		this->actionRecentFile[i]->setVisible(true);
	}
	for (int j = numRecentFiles; j < maxRecentFiles; ++j)
		this->actionRecentFile[j]->setVisible(false);

	// If we had to prune the list, then save the cleaned list
	if (originalNumRecentFiles != numRecentFiles)
		settings.setValue("recentFileList", files);
}

void MainWindow::actionOpenExample()
{
	QAction *action = qobject_cast<QAction *>(sender());
	if (action) {
		openFile(examplesdir + QDir::separator() + action->text());
	}
}

void MainWindow::actionSave()
{
	if (this->fileName.isEmpty()) {
		actionSaveAs();
	}
	else {
		setCurrentOutput();
		QFile file(this->fileName);
		if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
			PRINTB("Failed to open file for writing: %s (%s)", this->fileName.toLocal8Bit().constData() % file.errorString().toLocal8Bit().constData());
			QMessageBox::warning(this, windowTitle(), tr("Failed to open file for writing:\n %1 (%2)")
					.arg(this->fileName).arg(file.errorString()));
		}
		else {
			QTextStream writer(&file);
			writer.setCodec("UTF-8");
			writer << this->editor->toPlainText();
			PRINTB("Saved design '%s'.", this->fileName.toLocal8Bit().constData());
			this->editor->setContentModified(false);
		}
		clearCurrentOutput();
		updateRecentFiles();
	}
}

void MainWindow::actionSaveAs()
{
	QString new_filename = QFileDialog::getSaveFileName(this, "Save File",
			this->fileName.isEmpty()?"Untitled.scad":this->fileName,
			"OpenSCAD Designs (*.scad)");
	if (!new_filename.isEmpty()) {
		if (QFileInfo(new_filename).suffix().isEmpty()) {
			new_filename.append(".scad");

			// Manual overwrite check since Qt doesn't do it, when using the
			// defaultSuffix property
			QFileInfo info(new_filename);
			if (info.exists()) {
				if (QMessageBox::warning(this, windowTitle(),
						 tr("%1 already exists.\nDo you want to replace it?").arg(info.fileName()),
						 QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) {
					return;
				}
			}
		}
		setFileName(new_filename);
		actionSave();
	}
}

void MainWindow::actionShowLibraryFolder()
{
	std::string path = PlatformUtils::libraryPath();
	if (!fs::exists(path)) {
		PRINTB("WARNING: Library path %s doesnt exist. Creating", path);
		if (!PlatformUtils::createLibraryPath()) {
			PRINTB("ERROR: Cannot create library path: %s",path);
		}
	}
	QString url = QString::fromStdString( path );
	//PRINTB("Opening file browser for %s", url.toStdString() );
	QDesktopServices::openUrl(QUrl::fromLocalFile( url ));
}

void MainWindow::actionReload()
{
	if (checkEditorModified()) {
		fileChangedOnDisk(); // force cached autoReloadId to update
		refreshDocument();
	}
}

void MainWindow::hideEditor()
{
	QSettings settings;
	if (editActionHide->isChecked()) {
		editor->hide();
		settings.setValue("view/hideEditor",true);
	} else {
		editor->show();
		settings.setValue("view/hideEditor",false);
	}
}

void MainWindow::pasteViewportTranslation()
{
	QTextCursor cursor = editor->textCursor();
	QString txt;
	txt.sprintf("[ %.2f, %.2f, %.2f ]", -qglview->cam.object_trans.x(), -qglview->cam.object_trans.y(), -qglview->cam.object_trans.z());
	cursor.insertText(txt);
}

void MainWindow::pasteViewportRotation()
{
	QTextCursor cursor = editor->textCursor();
	QString txt;
	txt.sprintf("[ %.2f, %.2f, %.2f ]",
		fmodf(360 - qglview->cam.object_rot.x() + 90, 360), fmodf(360 - qglview->cam.object_rot.y(), 360), fmodf(360 - qglview->cam.object_rot.z(), 360));
	cursor.insertText(txt);
}

void MainWindow::updateTemporalVariables()
{
	this->top_ctx.set_variable("$t", Value(this->e_tval->text().toDouble()));
	
	Value::VectorType vpt;
	vpt.push_back(Value(-qglview->cam.object_trans.x()));
	vpt.push_back(Value(-qglview->cam.object_trans.y()));
	vpt.push_back(Value(-qglview->cam.object_trans.z()));
	this->top_ctx.set_variable("$vpt", Value(vpt));
	
	Value::VectorType vpr;
	vpr.push_back(Value(fmodf(360 - qglview->cam.object_rot.x() + 90, 360)));
	vpr.push_back(Value(fmodf(360 - qglview->cam.object_rot.y(), 360)));
	vpr.push_back(Value(fmodf(360 - qglview->cam.object_rot.z(), 360)));
	top_ctx.set_variable("$vpr", Value(vpr));
}

bool MainWindow::fileChangedOnDisk()
{
	if (!this->fileName.isEmpty()) {
		struct stat st;
		memset(&st, 0, sizeof(struct stat));
		bool valid = (stat(this->fileName.toLocal8Bit(), &st) == 0);
		// If file isn't there, just return and use current editor text
		if (!valid) return false;

		std::string newid = str(boost::format("%x.%x") % st.st_mtime % st.st_size);

		if (newid != this->autoReloadId) {
			this->autoReloadId = newid;
			return true;
		}
	}
	return false;
}

/*!
	Returns true if anything was compiled.
*/
void MainWindow::compileTopLevelDocument()
{
	updateTemporalVariables();
	
	this->last_compiled_doc = editor->toPlainText();
	std::string fulltext = 
		std::string(this->last_compiled_doc.toLocal8Bit().constData()) +
		"\n" + commandline_commands;
	
	delete this->root_module;
	this->root_module = NULL;
	
	this->root_module = parse(fulltext.c_str(),
														this->fileName.isEmpty() ? 
														"" : 
														QFileInfo(this->fileName).absolutePath().toLocal8Bit(), 
														false);
	
	if (!animate_panel->isVisible()) {
		highlighter->unhighlightLastError();
		if (!this->root_module) {
			QTextCursor cursor = editor->textCursor();
			cursor.setPosition(parser_error_pos);
			editor->setTextCursor(cursor);
			highlighter->highlightError( parser_error_pos );
		}
	}
}

void MainWindow::checkAutoReload()
{
	if (!this->fileName.isEmpty()) {
		actionReloadRenderCSG();
	}
}

void MainWindow::autoReloadSet(bool on)
{
	QSettings settings;
	settings.setValue("design/autoReload",designActionAutoReload->isChecked());
	if (on) {
		autoReloadTimer->start(200);
	} else {
		autoReloadTimer->stop();
	}
}

bool MainWindow::checkEditorModified()
{
	if (editor->isContentModified()) {
		QMessageBox::StandardButton ret;
		ret = QMessageBox::warning(this, "Application",
				"The document has been modified.\n"
				"Do you really want to reload the file?",
				QMessageBox::Yes | QMessageBox::No);
		if (ret != QMessageBox::Yes) {
			designActionAutoReload->setChecked(false);
			return false;
		}
	}
	return true;
}

void MainWindow::actionReloadRenderCSG()
{
	if (GuiLocker::isLocked()) return;
	GuiLocker::lock();
	autoReloadTimer->stop();
	setCurrentOutput();

	// PRINT("Parsing design (AST generation)...");
	// QApplication::processEvents();
	this->afterCompileSlot = "csgReloadRender";
	this->procevents = true;
	compile(true);
}

void MainWindow::csgReloadRender()
{
	if (this->root_node) compileCSG(true);

	// Go to non-CGAL view mode
	if (viewActionThrownTogether->isChecked()) {
		viewModeThrownTogether();
	}
	else {
#ifdef ENABLE_OPENCSG
		viewModeOpenCSG();
#else
		viewModeThrownTogether();
#endif
	}
	compileEnded();
}

void MainWindow::actionRenderCSG()
{
	if (GuiLocker::isLocked()) return;
	GuiLocker::lock();
	autoReloadTimer->stop();
	setCurrentOutput();

	PRINT("Parsing design (AST generation)...");
	QApplication::processEvents();
	this->afterCompileSlot = "csgRender";
	this->procevents = !viewActionAnimate->isChecked();
	compile(false);
}

void MainWindow::csgRender()
{
	if (this->root_node) compileCSG(!viewActionAnimate->isChecked());

	// Go to non-CGAL view mode
	if (viewActionThrownTogether->isChecked()) {
		viewModeThrownTogether();
	}
	else {
#ifdef ENABLE_OPENCSG
		viewModeOpenCSG();
#else
		viewModeThrownTogether();
#endif
	}

	if (viewActionAnimate->isChecked() && e_dump->isChecked()) {
		QImage img = this->qglview->grabFrameBuffer();
		QString filename;
		double s = this->e_fsteps->text().toDouble();
		double t = this->e_tval->text().toDouble();
		filename.sprintf("frame%05d.png", int(round(s*t)));
		img.save(filename, "PNG");
	}
	
	compileEnded();
}

#ifdef ENABLE_CGAL

void MainWindow::actionRenderCGAL()
{
	if (GuiLocker::isLocked()) return;
	GuiLocker::lock();
	autoReloadTimer->stop();
	setCurrentOutput();

	PRINT("Parsing design (AST generation)...");
	QApplication::processEvents();
	this->afterCompileSlot = "cgalRender";
	this->procevents = true;
	compile(false);
}

void MainWindow::cgalRender()
{
	if (!this->root_module || !this->root_node) {
		return;
	}

	this->qglview->setRenderer(NULL);
	delete this->cgalRenderer;
	this->cgalRenderer = NULL;
	if (this->root_N) {
		delete this->root_N;
		this->root_N = NULL;
	}

	PRINT("Rendering Polygon Mesh using CGAL...");

	this->progresswidget = new ProgressWidget(this);
	connect(this->progresswidget, SIGNAL(requestShow()), this, SLOT(showProgress()));

	progress_report_prep(this->root_node, report_func, this);

	this->cgalworker->start(this->tree);
}

void MainWindow::actionRenderCGALDone(CGAL_Nef_polyhedron *root_N)
{
	progress_report_fin();

	if (root_N) {
		PolySetCache::instance()->print();
#ifdef ENABLE_CGAL
		CGALCache::instance()->print();
#endif
		if (!root_N->isNull()) {
			if (root_N->dim == 2) {
				PRINT("   Top level object is a 2D object:");
				PRINTB("   Empty:      %6s", (root_N->p2->is_empty() ? "yes" : "no"));
				PRINTB("   Plane:      %6s", (root_N->p2->is_plane() ? "yes" : "no"));
				PRINTB("   Vertices:   %6d", root_N->p2->explorer().number_of_vertices());
				PRINTB("   Halfedges:  %6d", root_N->p2->explorer().number_of_halfedges());
				PRINTB("   Edges:      %6d", root_N->p2->explorer().number_of_edges());
				PRINTB("   Faces:      %6d", root_N->p2->explorer().number_of_faces());
				PRINTB("   FaceCycles: %6d", root_N->p2->explorer().number_of_face_cycles());
				PRINTB("   ConnComp:   %6d", root_N->p2->explorer().number_of_connected_components());
			}
			
			if (root_N->dim == 3) {
				PRINT("   Top level object is a 3D object:");
				PRINTB("   Simple:     %6s", (root_N->p3->is_simple() ? "yes" : "no"));
				PRINTB("   Valid:      %6s", (root_N->p3->is_valid() ? "yes" : "no"));
				PRINTB("   Vertices:   %6d", root_N->p3->number_of_vertices());
				PRINTB("   Halfedges:  %6d", root_N->p3->number_of_halfedges());
				PRINTB("   Edges:      %6d", root_N->p3->number_of_edges());
				PRINTB("   Halffacets: %6d", root_N->p3->number_of_halffacets());
				PRINTB("   Facets:     %6d", root_N->p3->number_of_facets());
				PRINTB("   Volumes:    %6d", root_N->p3->number_of_volumes());
			}
		}

		int s = this->progresswidget->elapsedTime() / 1000;
		PRINTB("Total rendering time: %d hours, %d minutes, %d seconds", (s / (60*60)) % ((s / 60) % 60) % (s % 60));

		this->root_N = root_N;
		if (!this->root_N->isNull()) {
			this->cgalRenderer = new CGALRenderer(*this->root_N);
			// Go to CGAL view mode
			if (viewActionCGALGrid->isChecked()) {
				viewModeCGALGrid();
			}
			else {
				viewModeCGALSurface();
			}
			
			PRINT("Rendering finished.");
		}
		else {
			PRINT("WARNING: No top level geometry to render");
		}
	}

	this->statusBar()->removeWidget(this->progresswidget);
	delete this->progresswidget;
	this->progresswidget = NULL;
	compileEnded();
    
    if(crossSectionMode) {
        crossSectionOutLines();
        
    }
}

#endif /* ENABLE_CGAL */

void MainWindow::actionDisplayAST()
{
	setCurrentOutput();
	QTextEdit *e = new QTextEdit(this);
	e->setWindowFlags(Qt::Window);
	e->setTabStopWidth(30);
	e->setWindowTitle("AST Dump");
	e->setReadOnly(true);
	if (root_module) {
		e->setPlainText(QString::fromLocal8Bit(root_module->dump("", "").c_str()));
	} else {
		e->setPlainText("No AST to dump. Please try compiling first...");
	}
	e->show();
	e->resize(600, 400);
	clearCurrentOutput();
}

void MainWindow::actionDisplayCSGTree()
{
	setCurrentOutput();
	QTextEdit *e = new QTextEdit(this);
	e->setWindowFlags(Qt::Window);
	e->setTabStopWidth(30);
	e->setWindowTitle("CSG Tree Dump");
	e->setReadOnly(true);
	if (this->root_node) {
		e->setPlainText(QString::fromLocal8Bit(this->tree.getString(*this->root_node).c_str()));
	} else {
		e->setPlainText("No CSG to dump. Please try compiling first...");
	}
	e->show();
	e->resize(600, 400);
	clearCurrentOutput();
}

void MainWindow::actionDisplayCSGProducts()
{
	setCurrentOutput();
	QTextEdit *e = new QTextEdit(this);
	e->setWindowFlags(Qt::Window);
	e->setTabStopWidth(30);
	e->setWindowTitle("CSG Products Dump");
	e->setReadOnly(true);
	e->setPlainText(QString("\nCSG before normalization:\n%1\n\n\nCSG after normalization:\n%2\n\n\nCSG rendering chain:\n%3\n\n\nHighlights CSG rendering chain:\n%4\n\n\nBackground CSG rendering chain:\n%5\n")
									.arg(root_raw_term ? QString::fromLocal8Bit(root_raw_term->dump().c_str()) : "N/A", 
											 root_norm_term ? QString::fromLocal8Bit(root_norm_term->dump().c_str()) : "N/A",
											 this->root_chain ? QString::fromLocal8Bit(this->root_chain->dump().c_str()) : "N/A", 
											 highlights_chain ? QString::fromLocal8Bit(highlights_chain->dump().c_str()) : "N/A", 
											 background_chain ? QString::fromLocal8Bit(background_chain->dump().c_str()) : "N/A"));
	e->show();
	e->resize(600, 400);
	clearCurrentOutput();
}

void MainWindow::seanRenderCGALAction(){
    cgalRender();
}

void MainWindow::openModelFileAction(){
    
    QString fileName = QFileDialog::getOpenFileName(this, tr("Open File"),"",tr("Files (*.*)"));
    const std::string file = fileName.toStdString();
    setCurrentOutput();
    PRINT(file);
    
    clearCurrentOutput();

    
}

void MainWindow::addCubeAction(){
    ModuleInstantiation * root_inst = new ModuleInstantiation("transform");
    
    TransformNode *transNode = new TransformNode(root_inst);
    double xAngle = (360 - qglview->cam.object_rot.x() + 90) *3.14159265 /180.0;
    double yAngle = (360 - qglview->cam.object_rot.y()) *3.14159265 /180.0;
    double zAngle = (360 - qglview->cam.object_rot.z()) *3.14159265 /180.0;
    
    cutPlaneCamAngleX = (360 - qglview->cam.object_rot.x() + 90);
    cutPlaneCamAngleY = (360 - qglview->cam.object_rot.y());
    cutPlaneCamAngleZ = (360 - qglview->cam.object_rot.z());
    
    transNode->matrix = Transform3d::Identity();
    cuttingPlaneMatrixRot = Transform3d::Identity();
    transNode->matrix(0,0)= cuttingPlaneMatrixRot(0,0) = cos(yAngle)*cos(zAngle);// cosY * cosZ
    transNode->matrix(0,1)= cuttingPlaneMatrixRot(0,1) = cos(zAngle)*sin(xAngle)*sin(yAngle) - cos(xAngle)*sin(zAngle);// cosZsinXsinY - cosXsinZ
    transNode->matrix(0,2)= cuttingPlaneMatrixRot(0,2) = cos(xAngle)*cos(zAngle)*sin(yAngle) + sin(xAngle)*sin(zAngle);// cosXcosZsinY + sinXsinZ
    transNode->matrix(1,0)= cuttingPlaneMatrixRot(1,0) = cos(yAngle)*sin(zAngle);// cosY * sinZ
    transNode->matrix(1,1)= cuttingPlaneMatrixRot(1,1) = cos(xAngle)*cos(zAngle) + sin(xAngle)*sin(yAngle)*sin(zAngle);// cosXcosZ+ sinXsinYsinZ
    transNode->matrix(1,2)= cuttingPlaneMatrixRot(1,2) = -1*cos(zAngle)*sin(xAngle)+cos(xAngle)*sin(yAngle)*sin(zAngle);// -cosZsinX+cosXsinYsinZ
    transNode->matrix(2,0)= cuttingPlaneMatrixRot(2,0) = -1*sin(yAngle);// -sinY
    transNode->matrix(2,1)= cuttingPlaneMatrixRot(2,1) = cos(yAngle)*sin(xAngle);// cosYsinX
    transNode->matrix(2,2)= cuttingPlaneMatrixRot(2,2) = cos(xAngle)*cos(yAngle);// cosX * cosY
    
    double screenAngle = -1 * atan2(qglview->slopeY, qglview->slopeX);
    cutPlaneScreenAngle = screenAngle*180/3.14159;
    
    Transform3d rotMatrix = Transform3d::Identity();
    
    rotMatrix(0,0)= cos(screenAngle); //cos
    rotMatrix(0,1)=  -1* sin(screenAngle);                        //-sin
    rotMatrix(1,0)=  sin(screenAngle);            //sin
    rotMatrix(1,1)=  cos(screenAngle);            //cos
    
    
    transNode->matrix= transNode->matrix * rotMatrix;
    cuttingPlaneMatrixRot = cuttingPlaneMatrixRot *rotMatrix;
    
    
    // find intersection
    /*
    double perpendicularSlope = -1.0 / qglview->slope;
    
    double xIntercept = - (0 - qglview->offset)/(perpendicularSlope- qglview->slope); //X = - (c2-c1)/(m2-m1)
    double yIntercept = qglview->slope * 0 - (qglview->offset * perpendicularSlope)/(perpendicularSlope - qglview->slope)  ; //Y = m1c2 - c1m2 / m2-m1
    
    double yInterceptCheck = (qglview->offset/qglview->slope - 0)/ (1/qglview->slope - 1/perpendicularSlope);
    
    double distance = sqrt(xIntercept*xIntercept + yIntercept*yIntercept); */
    
    
    for(int i=0; i <4; i++){
        for(int j=0; j< 4; j++){
            
            setCurrentOutput();
            PRINTB("pre %f",(transNode->matrix(i,j)));
            clearCurrentOutput();
        }
    }
    
    

    
    
    Transform3d transMatrix = Transform3d::Identity();
    
    qglview->lineRegressionWorldSpace();
    
    setCurrentOutput();
	PRINTB("y dist %f",(qglview->distanceToLine()));
	PRINTB("slopeworld %f",(qglview->slopeWorld));
	PRINTB("offsetworld %f",(qglview->offsetWorld));
    clearCurrentOutput();
    
    transMatrix(0,3)=  -500;//0 - qglview->cam.viewer_distance/20;
    transMatrix(1,3)= qglview->distanceToLine();//-1*distance*qglview->cam.viewer_distance/5000.0;//0 - qglview->cam.viewer_distance/20; //qglview->offset * qglview->cam.viewer_distance / 2000.0;
    transMatrix(2,3)=  -500;
    transNode->matrix= transNode->matrix * transMatrix;
    for(int i=0; i <4; i++){
        for(int j=0; j< 4; j++){
            cuttingPlaneMatrix.push_back(transNode->matrix(i,j));
            setCurrentOutput();
            PRINTB("offset %f",(transNode->matrix(i,j)));
            clearCurrentOutput();
        }
    }
    
    Transform3d transMatrix2 = Transform3d::Identity();
    transMatrix2(1,3)= qglview->distanceToLine();
    cuttingPlaneMatrixRot= cuttingPlaneMatrixRot * transMatrix2;
    
    
    ModuleInstantiation * root_inst2 = new ModuleInstantiation("cube");
    
    PrimitiveNode *node = new PrimitiveNode(root_inst2, CUBE);
    node->center = false;
	node->x = node->y = node->z = node->h = node->r1 = node->r2 = 1000;
    transNode->children.push_back(node);
    
    this->root_node->children[0]->children.push_back(transNode); //
    csgRender();
    
    setCurrentOutput();
    PRINTB("screenAngle %f",  (screenAngle*180.0/3.14159));
    //PRINTB("offset %f",  yInterceptCheck);
    clearCurrentOutput();
    
}

void MainWindow::addCuttingPlaneAfter(){
    ModuleInstantiation * root_inst = new ModuleInstantiation("transform");
    
    TransformNode *transNode = new TransformNode(root_inst);

    transNode->matrix = Transform3d::Identity();
    
    int count = 0;
    for(int i=0; i <4; i++){
        for(int j=0; j< 4; j++){
            transNode->matrix(i,j) = cuttingPlaneMatrix[count];
            setCurrentOutput();
            PRINTB("offset %f",(transNode->matrix(i,j)));
            clearCurrentOutput();
            count++;
        }
    }
    
    ModuleInstantiation * root_inst2 = new ModuleInstantiation("cube");
    
    PrimitiveNode *node = new PrimitiveNode(root_inst2, CUBE);
    node->center = false;
	node->x = node->y = node->z = node->h = node->r1 = node->r2 = 1000;
    transNode->children.push_back(node);
    
    this->root_node->children[0]->children.push_back(transNode); //
    csgRender();

    
    
}

void MainWindow::addCuttingPlaneAfterNoRender(){
    ModuleInstantiation * root_inst = new ModuleInstantiation("transform");
    
    TransformNode *transNode = new TransformNode(root_inst);
    
    transNode->matrix = Transform3d::Identity();
    
    int count = 0;
    for(int i=0; i <4; i++){
        for(int j=0; j< 4; j++){
            transNode->matrix(i,j) = cuttingPlaneMatrix[count];
            setCurrentOutput();
            PRINTB("offset %f",(transNode->matrix(i,j)));
            clearCurrentOutput();
            count++;
        }
    }
    
    ModuleInstantiation * root_inst2 = new ModuleInstantiation("cube");
    
    PrimitiveNode *node = new PrimitiveNode(root_inst2, CUBE);
    node->center = false;
	node->x = node->y = node->z = node->h = node->r1 = node->r2 = 1000;
    transNode->children.push_back(node);
    
    this->root_node->children[0]->children.push_back(transNode); //
    
    
    
    
}


void MainWindow::addScrewHole(double xPos, double yPos){
    ModuleInstantiation * root_inst = new ModuleInstantiation("transform");
    
    TransformNode *transNode = new TransformNode(root_inst);
    
    transNode->matrix = Transform3d::Identity();
    
    Transform3d transMatrix2 = cuttingPlaneMatrixRot;
    
    Transform3d transPos =  Transform3d::Identity();
    transPos(0,3) = xPos;
    transPos(1,3) = yPos;
    transMatrix2 = transMatrix2 * transPos;
    int count = 0;
    for(int i=0; i <4; i++){
        for(int j=0; j< 4; j++){
            transNode->matrix(i,j) = transMatrix2(i,j);
            setCurrentOutput();
            PRINTB("offset %f",(transNode->matrix(i,j)));
            clearCurrentOutput();
            count++;
        }
    }
    
    ModuleInstantiation * root_inst2 = new ModuleInstantiation("cylinder");
    
    PrimitiveNode *node = new PrimitiveNode(root_inst2, CYLINDER);
    node->center = false;
	node->x = node->y = node->z = node->h = node->r1 = node->r2 = 100;
    transNode->children.push_back(node);
    
    this->root_node->children[0]->children.push_back(transNode); //
    
    
    
    
}

void MainWindow::setSliderVal(int val)
{
    int numChildren = this->root_node->getChildren()[0]->getChildren()[1]->getChildren().size();
    /*std::stringstream ss1;
    ss1 << numChildren;
    std::string str1 = ss1.str();
    
    PRINT("NUM CHILDREN");
    PRINT(str1);*/
    
    
    std::vector<AbstractNode*>  children = this->root_node->getChildren()[0]->getChildren()[1]->getChildren();
    
    Transform3d mat = ((TransformNode*)(children[0]))->matrix;
    
    
    ((TransformNode*)(children[0]))->matrix(2,3)=val/5.0; //- 57;
    
    /*
    setCurrentOutput();
    PRINT(this->tree.getString(*this->root_node));
    PRINT(this->root_node->getChildren()[1]->getChildren()[1]->getChildren()[0]->name());
    if(this->root_node->getChildren()[1]->getChildren()[1]->getChildren()[0]->modinst->isHighlight()){
        
        PRINT("BACKGROUND");
    }
    if(this->root_node->getChildren()[1]->getChildren()[1]->getChildren()[0]->modinst->isBackground()){
        
        PRINT("HIGHLIGHT");
    }
    if(this->root_node->getChildren()[1]->getChildren()[1]->getChildren()[0]->modinst->isRoot()){
        
        PRINT("Root");
    }
       
    
    if(this->root_node->getChildren()[1]->getChildren()[1]->getChildren()[0]->modinst->isHighlight()){
        
        PRINT("BACKGROUND");
    }
    if(this->root_node->getChildren()[1]->getChildren()[1]->getChildren()[0]->modinst->isBackground()){
        
        PRINT("HIGHLIGHT");
    }
    clearCurrentOutput();
    */
    
    csgRender();
    //cgalRender();
}

void MainWindow::actionTestSeanCode()
{
    setCurrentOutput();
    PRINT("Hello World");
    clearCurrentOutput();
    /*
    setCurrentOutput();
	QTextEdit *e = new QTextEdit(this);
	e->setWindowFlags(Qt::Window);
	e->setTabStopWidth(30);
	e->setWindowTitle("Test");
	e->setReadOnly(true);
	if (root_module) {
		e->setPlainText(QString::fromLocal8Bit(root_module->dump("", "").c_str()));
	} else {
		e->setPlainText("No AST to dump. Please try compiling first...");
	}
    /*e->setPlainText("HelloWorld");
	e->show();
	e->resize(600, 400);
	clearCurrentOutput();*/
    
    const char *outfilename = "testOut";
    
    ModuleContext top_ctx;
	top_ctx.registerBuiltin();
    
	//FileModule *root_module;
	//ModuleInstantiation root_inst("group");
    
    delete this->root_module;
	this->root_module = NULL;
	
	this->root_module = parse("difference(){sphere( r=25, $fn = 50); translate([0, 0, 0]) rotate(a=-25, v=[0,1,0]) translate([12, 72, -57]) {sphere( r=25, $fn = 50);}}  ","",false); //cube(); // translate([0, 0, -2]){ sphere($fn = 50);}} //import(\"fileRepaired.stl\", convexity=3)
    instantiateRoot();
    
    int numChildren = this->root_node->getChildren()[0]->getChildren()[1]->getChildren().size();
    std::stringstream ss1;
    ss1 << numChildren;
    std::string str1 = ss1.str();
    setCurrentOutput();
    PRINT("NUM CHILDREN");
    PRINT(str1);
    clearCurrentOutput();
    
    ModuleInstantiation * root_inst = new ModuleInstantiation("transform");
    /*root_inst->tag_root = false;
    root_inst->tag_highlight = false;
    root_inst->tag_background = false;*/
    
    TransformNode *transNode = new TransformNode(root_inst);
    transNode->matrix = Transform3d::Identity();
    transNode->matrix(2,3)=-5;
    
    ModuleInstantiation * root_inst2 = new ModuleInstantiation("transform");
    /*root_inst->tag_root = false;
     root_inst->tag_highlight = false;
     root_inst->tag_background = false;*/
    
    PrimitiveNode *node = new PrimitiveNode(root_inst2, CUBE);
    node->center = false;
	node->x = node->y = node->z = node->h = node->r1 = node->r2 = 1;
    transNode->children.push_back(node);
    
    //this->root_node->getChildren()[0]->getChildren()[1]->children.push_back(transNode);
    
    //std::vector<AbstractNode*>  children = this->root_node->getChildren()[1]->getChildren();
    //children.push_back(transNode);
    
    numChildren = this->root_node->getChildren()[0]->getChildren()[1]->getChildren().size();
    ss1 << numChildren;
    str1 = ss1.str();
    setCurrentOutput();
    PRINT("NUM CHILDREN");
    PRINT(str1);
    
    PRINT(this->tree.getString(*this->root_node));
    clearCurrentOutput();
    
    csgRender();
    
    //compileCSG(false);
    /*
    AbstractNode::resetIndexCounter();
	AbstractNode *absolute_root_node = root_module->instantiate(&top_ctx, &root_inst);
	AbstractNode *root_node;
	// Do we have an explicit root node (! modifier)?
	if (!(root_node = find_root_tag(absolute_root_node))) root_node = absolute_root_node;
    
	Tree tree(root_node);
    
    setCurrentOutput();
    PRINT(tree.getString(*root_node));
    PRINT("printing num children");
    int numChildren = root_node->getChildren()[1]->getChildren().size();
    std::stringstream ss1;
    ss1 << numChildren;
    std::string str1 = ss1.str();
    PRINT(str1);
    
    
    std::vector<AbstractNode*>  children = root_node->getChildren();
    PRINT(children[1]->name());
    Transform3d mat = ((TransformNode*)(children[1]))->matrix;
    
    std::stringstream stream;
    
	stream << "multmatrix([";
	for (int j=0;j<4;j++) {
		stream << "[";
		for (int i=0;i<4;i++) {
			Value v( mat(j, i) );
			stream << v;
			if (i != 3) stream << ", ";
		}
		stream << "]";
		if (j != 3) stream << ", ";
	}
	stream << "])";
    std::string str3 = stream.str();
    
    PRINT(str3);
    ((TransformNode*)(children[1]))->matrix(2,3)=-5; //=  Matrix3d::Zero();
    
    mat = ((TransformNode*)(children[1]))->matrix;
    
    std::stringstream stream4;
    stream4 << "multmatrix([";
	for (int j=0;j<4;j++) {
		stream4 << "[";
		for (int i=0;i<4;i++) {
			Value v( mat(j, i) );
			stream4 << v;
			if (i != 3) stream4 << ", ";
		}
		stream4 << "]";
		if (j != 3) stream4 << ", ";
	}
	stream4 << "])";
    std::string str4 = stream4.str();
     PRINT(str4);
    
    int rad = ((PrimitiveNode*)children[0])->r1;
    
    std::stringstream ss;
    ss << rad;
    std::string str = ss.str();
    PRINT(str);
    
    
    ((PrimitiveNode*)children[0])->r1 = 15;
    rad = ((PrimitiveNode*)children[0])->r1;
    ss << rad;
    str = ss.str();
    PRINT(str);
    
    PRINT(tree.getString(*root_node));
    clearCurrentOutput();
    
    this->root_node = root_node; */
    
    
    
    /*
    
	CsgInfo csgInfo = CsgInfo();
	if ( !csgInfo.compile_chains( tree ) ) PRINT("bad");
    
    try {
		csgInfo.glview = new OffscreenView(512,512);
	} catch (int error) {
		fprintf(stderr,"Can't create OpenGL OffscreenView. Code: %i. Exiting.\n", error);
		exit(1);
	}
    
    Camera camera(Camera::VECTOR);
	camera.center << 0,0,0;
	double radius = 1.0;
    
	if (csgInfo.root_chain) {
		BoundingBox bbox = csgInfo.root_chain->getBoundingBox();
		camera.center = (bbox.min() + bbox.max()) / 2;
		radius = (bbox.max() - bbox.min()).norm() / 2;
	}
	Vector3d cameradir(1, 1, -0.5);
	camera.eye = camera.center - radius*1.8*cameradir;
	csgInfo.glview->setCamera(camera);
    
	OpenCSGRenderer opencsgRenderer(csgInfo.root_chain, csgInfo.highlights_chain, csgInfo.background_chain, csgInfo.glview->shaderinfo);
	ThrownTogetherRenderer thrownTogetherRenderer(csgInfo.root_chain, csgInfo.highlights_chain, csgInfo.background_chain);
    
   
		csgInfo.glview->setRenderer(&opencsgRenderer);
    
    
    OpenCSG::setContext(0);
	OpenCSG::setOption(OpenCSG::OffscreenSetting, OpenCSG::FrameBufferObject);
    
	csgInfo.glview->paintGL();
    
    if (outfilename) csgInfo.glview->save(outfilename);
    
    setCurrentOutput();
    PRINT("Did not crash!");
    clearCurrentOutput(); */
}

#ifdef ENABLE_CGAL
void MainWindow::actionExportSTLorOFF(bool stl_mode)
#else
void MainWindow::actionExportSTLorOFF(bool)
#endif
{
	if (GuiLocker::isLocked()) return;
	GuiLocker lock;
#ifdef ENABLE_CGAL
	setCurrentOutput();

	if (!this->root_N) {
		PRINT("Nothing to export! Try building first (press F6).");
		clearCurrentOutput();
		return;
	}

	if (this->root_N->dim != 3) {
		PRINT("Current top level object is not a 3D object.");
		clearCurrentOutput();
		return;
	}

	if (!this->root_N->p3->is_simple()) {
		PRINT("Object isn't a valid 2-manifold! Modify your design. See http://en.wikibooks.org/wiki/OpenSCAD_User_Manual/STL_Import_and_Export");
		clearCurrentOutput();
		return;
	}

	QString suffix = stl_mode ? ".stl" : ".off";
	QString stl_filename = QFileDialog::getSaveFileName(this,
			stl_mode ? "Export STL File" : "Export OFF File", 
			this->fileName.isEmpty() ? "Untitled"+suffix : QFileInfo(this->fileName).baseName()+suffix,
			stl_mode ? "STL Files (*.stl)" : "OFF Files (*.off)");
	if (stl_filename.isEmpty()) {
		PRINTB("No filename specified. %s export aborted.", (stl_mode ? "STL" : "OFF"));
		clearCurrentOutput();
		return;
	}

	std::ofstream fstream(stl_filename.toUtf8());
	if (!fstream.is_open()) {
		PRINTB("Can't open file \"%s\" for export", stl_filename.toLocal8Bit().constData());
	}
	else {
		if (stl_mode) export_stl(this->root_N, fstream);
		else export_off(this->root_N, fstream);
		fstream.close();

		PRINTB("%s export finished.", (stl_mode ? "STL" : "OFF"));
	}

	clearCurrentOutput();
#endif /* ENABLE_CGAL */
}

void MainWindow::actionExportSTL()
{
	actionExportSTLorOFF(true);
}

void MainWindow::actionExportOFF()
{
	actionExportSTLorOFF(false);
}

void MainWindow::actionExportDXF()
{
#ifdef ENABLE_CGAL
	setCurrentOutput();

	if (!this->root_N) {
		PRINT("Nothing to export! Try building first (press F6).");
		clearCurrentOutput();
		return;
	}

	if (this->root_N->dim != 2) {
		PRINT("Current top level object is not a 2D object.");
		clearCurrentOutput();
		return;
	}

	QString dxf_filename = QFileDialog::getSaveFileName(this,
			"Export DXF File", 
			this->fileName.isEmpty() ? "Untitled.dxf" : QFileInfo(this->fileName).baseName()+".dxf",
			"DXF Files (*.dxf)");
	if (dxf_filename.isEmpty()) {
		PRINT("No filename specified. DXF export aborted.");
		clearCurrentOutput();
		return;
	}

	std::ofstream fstream(dxf_filename.toUtf8());
	if (!fstream.is_open()) {
		PRINTB("Can't open file \"%s\" for export", dxf_filename.toLocal8Bit().constData());
	}
	else {
		export_dxf(this->root_N, fstream);
		fstream.close();
		PRINT("DXF export finished.");
	}

	clearCurrentOutput();
#endif /* ENABLE_CGAL */
}

void MainWindow::actionExportCSG()
{
	setCurrentOutput();

	if (!this->root_node) {
		PRINT("Nothing to export. Please try compiling first...");
		clearCurrentOutput();
		return;
	}

	QString csg_filename = QFileDialog::getSaveFileName(this, "Export CSG File", 
																											this->fileName.isEmpty() ? "Untitled.csg" : QFileInfo(this->fileName).baseName()+".csg",
																											"CSG Files (*.csg)");
	if (csg_filename.isEmpty()) {
		PRINT("No filename specified. CSG export aborted.");
		clearCurrentOutput();
		return;
	}

	std::ofstream fstream(csg_filename.toUtf8());
	if (!fstream.is_open()) {
		PRINTB("Can't open file \"%s\" for export", csg_filename.toLocal8Bit().constData());
	}
	else {
		fstream << this->tree.getString(*this->root_node) << "\n";
		fstream.close();
		PRINT("CSG export finished.");
	}

	clearCurrentOutput();
}

void MainWindow::actionExportImage()
{
	setCurrentOutput();

	QString img_filename = QFileDialog::getSaveFileName(this,
			"Export Image", "", "PNG Files (*.png)");
	if (img_filename.isEmpty()) {
		PRINT("No filename specified. Image export aborted.");
	} else {
		qglview->save(img_filename.toLocal8Bit().constData());
	}
	clearCurrentOutput();
	return;
}

void MainWindow::actionFlushCaches()
{
	PolySetCache::instance()->clear();
#ifdef ENABLE_CGAL
	CGALCache::instance()->clear();
#endif
	dxf_dim_cache.clear();
	dxf_cross_cache.clear();
	ModuleCache::instance()->clear();
}

void MainWindow::viewModeActionsUncheck()
{
	viewActionOpenCSG->setChecked(false);
#ifdef ENABLE_CGAL
	viewActionCGALSurfaces->setChecked(false);
	viewActionCGALGrid->setChecked(false);
#endif
	viewActionThrownTogether->setChecked(false);
}

#ifdef ENABLE_OPENCSG

/*!
	Go to the OpenCSG view mode.
	Falls back to thrown together mode if OpenCSG is not available
*/
void MainWindow::viewModeOpenCSG()
{
	if (this->qglview->hasOpenCSGSupport()) {
		viewModeActionsUncheck();
		viewActionOpenCSG->setChecked(true);
		this->qglview->setRenderer(this->opencsgRenderer ? (Renderer *)this->opencsgRenderer : (Renderer *)this->thrownTogetherRenderer);
		this->qglview->updateGL();
	} else {
		viewModeThrownTogether();
	}
}

#endif /* ENABLE_OPENCSG */

#ifdef ENABLE_CGAL

void MainWindow::viewModeCGALSurface()
{
	viewModeActionsUncheck();
	viewActionCGALSurfaces->setChecked(true);
	this->qglview->setShowFaces(true);
	this->qglview->setRenderer(this->cgalRenderer);
	this->qglview->updateGL();
}

void MainWindow::viewModeCGALGrid()
{
	viewModeActionsUncheck();
	viewActionCGALGrid->setChecked(true);
	this->qglview->setShowFaces(false);
	this->qglview->setRenderer(this->cgalRenderer);
	this->qglview->updateGL();
}

#endif /* ENABLE_CGAL */

void MainWindow::viewModeThrownTogether()
{
	viewModeActionsUncheck();
	viewActionThrownTogether->setChecked(true);
	this->qglview->setRenderer(this->thrownTogetherRenderer);
	this->qglview->updateGL();
}

void MainWindow::viewModeShowEdges()
{
	QSettings settings;
	settings.setValue("view/showEdges",viewActionShowEdges->isChecked());
	this->qglview->setShowEdges(viewActionShowEdges->isChecked());
	this->qglview->updateGL();
}

void MainWindow::viewModeShowAxes()
{
	QSettings settings;
	settings.setValue("view/showAxes",viewActionShowAxes->isChecked());
	this->qglview->setShowAxes(viewActionShowAxes->isChecked());
	this->qglview->updateGL();
}

void MainWindow::viewModeShowCrosshairs()
{
	QSettings settings;
	settings.setValue("view/showCrosshairs",viewActionShowCrosshairs->isChecked());
	this->qglview->setShowCrosshairs(viewActionShowCrosshairs->isChecked());
	this->qglview->updateGL();
}

void MainWindow::viewModeAnimate()
{
	if (viewActionAnimate->isChecked()) {
		animate_panel->show();
		actionRenderCSG();
		updatedFps();
	} else {
		animate_panel->hide();
		animate_timer->stop();
	}
}

void MainWindow::animateUpdateDocChanged()
{
	QString current_doc = editor->toPlainText();
	if (current_doc != last_compiled_doc)
		animateUpdate();
}

void MainWindow::animateUpdate()
{
	if (animate_panel->isVisible()) {
		bool fps_ok;
		double fps = this->e_fps->text().toDouble(&fps_ok);
		if (fps_ok && fps <= 0 && !animate_timer->isActive()) {
			animate_timer->stop();
			animate_timer->setSingleShot(true);
			animate_timer->setInterval(50);
			animate_timer->start();
		}
	}
}

void MainWindow::viewAngleTop()
{
	qglview->cam.object_rot << 90,0,0;
	this->qglview->updateGL();
}

void MainWindow::viewAngleBottom()
{
	qglview->cam.object_rot << 270,0,0;
	this->qglview->updateGL();
}

void MainWindow::viewAngleLeft()
{
	qglview->cam.object_rot << 0,0,90;
	this->qglview->updateGL();
}

void MainWindow::viewAngleRight()
{
	qglview->cam.object_rot << 0,0,270;
	this->qglview->updateGL();
}

void MainWindow::viewAngleFront()
{
	qglview->cam.object_rot << 0,0,0;
	this->qglview->updateGL();
}

void MainWindow::viewAngleBack()
{
	qglview->cam.object_rot << 0,0,180;
	this->qglview->updateGL();
}

void MainWindow::viewAngleDiagonal()
{
	qglview->cam.object_rot << 35,0,-25;
	this->qglview->updateGL();
}

void MainWindow::viewCenter()
{
	qglview->cam.object_trans << 0,0,0;
	this->qglview->updateGL();
}

void MainWindow::viewPerspective()
{
	QSettings settings;
	settings.setValue("view/orthogonalProjection",false);
	viewActionPerspective->setChecked(true);
	viewActionOrthogonal->setChecked(false);
	this->qglview->setOrthoMode(false);
	this->qglview->updateGL();
}

void MainWindow::viewOrthogonal()
{
	QSettings settings;
	settings.setValue("view/orthogonalProjection",true);
	viewActionPerspective->setChecked(false);
	viewActionOrthogonal->setChecked(true);
	this->qglview->setOrthoMode(true);
	this->qglview->updateGL();
}

void MainWindow::hideConsole()
{
	QSettings settings;
	if (viewActionHide->isChecked()) {
		console->hide();
		settings.setValue("view/hideConsole",true);
	} else {
		console->show();
		settings.setValue("view/hideConsole",false);
	}
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
	if (event->mimeData()->hasUrls())
		event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *event)
{
	setCurrentOutput();
	const QList<QUrl> urls = event->mimeData()->urls();
	for (int i = 0; i < urls.size(); i++) {
		if (urls[i].scheme() != "file")
			continue;
		openFile(urls[i].toLocalFile());
	}
	clearCurrentOutput();
}

void
MainWindow::helpAbout()
{
	qApp->setWindowIcon(QApplication::windowIcon());
	AboutDialog *dialog = new AboutDialog(this);
	dialog->exec();
	//QMessageBox::information(this, "About OpenSCAD", QString(helptitle) + QString(copyrighttext));
}

void
MainWindow::helpHomepage()
{
	QDesktopServices::openUrl(QUrl("http://openscad.org/"));
}

void
MainWindow::helpManual()
{
	QDesktopServices::openUrl(QUrl("http://www.openscad.org/documentation.html"));
}

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)
void MainWindow::helpLibrary()
{
	QString libinfo;
	libinfo.sprintf("Boost version: %s\n"
									"Eigen version: %d.%d.%d\n"
									"CGAL version: %s\n"
									"OpenCSG version: %s\n"
									"Qt version: %s\n\n",
									BOOST_LIB_VERSION,
									EIGEN_WORLD_VERSION, EIGEN_MAJOR_VERSION, EIGEN_MINOR_VERSION,
									TOSTRING(CGAL_VERSION),
									OPENCSG_VERSION_STRING,
									qVersion());

#if defined( __MINGW64__ )
	libinfo += QString("Compiled for MingW64\n\n");
#elif defined( __MINGW32__ )
	libinfo += QString("Compiled for MingW32\n\n");
#endif

	if (!this->openglbox) {
    this->openglbox = new QMessageBox(QMessageBox::Information, 
                                      "OpenGL Info", "OpenSCAD Detailed Library Info                  ",
                                      QMessageBox::Ok, this);
	}
  this->openglbox->setDetailedText(libinfo + QString(qglview->getRendererInfo().c_str()));
	this->openglbox->show();
}

/*!
	FIXME: In MDI mode, should this be called on both reload functions?
 */
bool
MainWindow::maybeSave()
{
	if (editor->isContentModified()) {
		QMessageBox::StandardButton ret;
		ret = QMessageBox::warning(this, "Application",
				"The document has been modified.\n"
				"Do you want to save your changes?",
				QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
		if (ret == QMessageBox::Save) {
			actionSave();
			return true; // FIXME: Should return false on error
		}
		else if (ret == QMessageBox::Cancel) {
			return false;
		}
	}
	return true;
}

void MainWindow::closeEvent(QCloseEvent *event)
{
	if (maybeSave()) {
		QSettings settings;
		settings.setValue("window/size", size());
		settings.setValue("window/position", pos());
		settings_setValueList("window/splitter1sizes",splitter1->sizes());
		settings_setValueList("window/splitter2sizes",splitter2->sizes());
		event->accept();
	} else {
		event->ignore();
	}
}

void
MainWindow::preferences()
{
	Preferences::inst()->show();
	Preferences::inst()->activateWindow();
	Preferences::inst()->raise();
}

void MainWindow::setFont(const QString &family, uint size)
{
	QFont font;
	if (!family.isEmpty()) font.setFamily(family);
	else font.setFixedPitch(true);
	if (size > 0)	font.setPointSize(size);
	font.setStyleHint(QFont::TypeWriter);
	editor->setFont(font);
}

void MainWindow::quit()
{
	QCloseEvent ev;
	QApplication::sendEvent(QApplication::instance(), &ev);
	if (ev.isAccepted()) QApplication::instance()->quit();
  // FIXME: Cancel any CGAL calculations
#ifdef Q_OS_MAC
	CocoaUtils::endApplication();
#endif
}

void MainWindow::consoleOutput(const std::string &msg, void *userdata)
{
	// Invoke the append function in the main thread in case the output
  // originates in a worker thread.
	MainWindow *thisp = static_cast<MainWindow*>(userdata);
	QMetaObject::invokeMethod(thisp->console, "append", Qt::QueuedConnection,
														Q_ARG(QString, QString::fromLocal8Bit(msg.c_str())));
}

void MainWindow::setCurrentOutput()
{
	set_output_handler(&MainWindow::consoleOutput, this);
}

void MainWindow::clearCurrentOutput()
{
	set_output_handler(NULL, NULL);
}

void MainWindow::openCSGSettingsChanged()
{
#ifdef ENABLE_OPENCSG
	OpenCSG::setOption(OpenCSG::AlgorithmSetting, Preferences::inst()->getValue("advanced/forceGoldfeather").toBool() ? OpenCSG::Goldfeather : OpenCSG::Automatic);
#endif
}

void MainWindow::cuttingPlaneAction()
{
    int maxPoints = this->qglview->drawnPoints.size();
    setCurrentOutput();
    for(int i=0; i < maxPoints; i++){
        
        PRINTB("x loc %f", this->qglview->drawnPoints[i].x());
    }
    QPointF point2 = this->qglview->GetOGLPos(this->qglview->drawnPoints[maxPoints-1].x(),this->qglview->drawnPoints[maxPoints-1].y());
    
    PRINTB("x loc world %d", point2.x());
    PRINTB("y loc world %d", point2.y());
    
    clearCurrentOutput();
    
    addCubeAction();
}

void MainWindow::insertionDirectionAction(){
    setCurrentOutput();
    for(int i =0; i < qglview->drawnInsertionPointsWorld.size(); i++){
        
        PRINTB("x loc world %f", qglview->drawnInsertionPointsWorld[i].x());
        PRINTB("y loc world %f", qglview->drawnInsertionPointsWorld[i].y());
        PRINTB("z loc world %f", qglview->drawnInsertionPointsWorld[i].z());
        
    }
    clearCurrentOutput();
}

void MainWindow::demoButtonAction() {
	targetFileName = "../sampleModels/target.stl";
	const std::string tFile = targetFileName.toStdString();
	
	enclosureFileName = "../sampleModels/enclosureSmall.stl";
	const std::string eFile = enclosureFileName.toStdString();
	
	setCurrentOutput();
	PRINT(tFile);
	PRINT(eFile);
	clearCurrentOutput();
	
	loadOriginalFiles();
}

void MainWindow::targetButtonOpenAction(){
    targetFileName = QFileDialog::getOpenFileName(this, tr("Open File"),"",tr("Files (*.*)"));
    const std::string file = targetFileName.toStdString();
    setCurrentOutput();
    PRINT(file);
    
    clearCurrentOutput();
}

void MainWindow::enclosureButtonAction(){
    enclosureFileName = QFileDialog::getOpenFileName(this, tr("Open File"),"",tr("Files (*.*)"));
    const std::string file = enclosureFileName.toStdString();
    setCurrentOutput();
    PRINT(file);
    
    clearCurrentOutput();
    
    loadOriginalFiles();
}

void MainWindow::alignButtonAction(){
    alignFileName = QFileDialog::getOpenFileName(this, tr("Open File"),"",tr("Files (*.*)"));
    const std::string file = alignFileName.toStdString();
    setCurrentOutput();
    PRINT(file);
    
    clearCurrentOutput();
}

void MainWindow::cuttingPlaneButtonAction(){
    
    qglview->cam.object_rot << cutPlaneCamAngleX,cutPlaneCamAngleY+cutPlaneScreenAngle,cutPlaneCamAngleZ;
	this->qglview->updateGL();
    
}

void MainWindow::insertionButtonAction(){
    xRot =  fmodf(360 - qglview->cam.object_rot.x() + 90, 360);
    yRot = fmodf(360 - qglview->cam.object_rot.y(), 360);
    zRot = fmodf(360 - qglview->cam.object_rot.z(), 360);
    
    setCurrentOutput();
    PRINTB("xRot %f", xRot);
    PRINTB("yRot %f", yRot);
    PRINTB("zRot %f", zRot);
    clearCurrentOutput();
    
    std::ofstream *out = new std::ofstream("rotate.mlx");
    if(!out->good()) {
        exit(1);
    }
    
    
    *out << "<!DOCTYPE FilterScript>" << std::endl;
    *out << "<FilterScript>" << std::endl;
    
    *out << "<filter name=\"Transform: Rotate\">" << std::endl;
    *out << "<Param enum_val0=\"X axis\" enum_val1=\"Y axis\" enum_cardinality=\"4\" enum_val2=\"Z axis\" enum_val3=\"custom axis\" type=\"RichEnum\" value=\"2\" name=\"rotAxis\"/>" << std::endl;
    *out << "<Param enum_val0=\"origin\" enum_val1=\"barycenter\" enum_cardinality=\"3\" enum_val2=\"custom point\" type=\"RichEnum\" value=\"0\" name=\"rotCenter\"/>" << std::endl;
    *out << "<Param type=\"RichDynamicFloat\" value=\""<< zRot <<"\" min=\"-360\" name=\"angle\" max=\"360\"/>" << std::endl;
    *out << "<Param type=\"RichBool\" value=\"false\" name=\"snapFlag\"/>" << std::endl;
    *out << "<Param x=\"0\" y=\"0\" z=\"0\" type=\"RichPoint3f\" name=\"customAxis\"/>" << std::endl;
    *out << "<Param x=\"0\" y=\"0\" z=\"0\" type=\"RichPoint3f\" name=\"customCenter\"/>" << std::endl;
    *out << "<Param type=\"RichFloat\" value=\"30\" name=\"snapAngle\"/>" << std::endl;
    *out << "<Param type=\"RichBool\" value=\"true\" name=\"Freeze\"/>" << std::endl;
    *out << "<Param type=\"RichBool\" value=\"false\" name=\"ToAll\"/>" << std::endl;
    *out << "</filter>" << std::endl;
    
    
    *out << "<filter name=\"Transform: Rotate\">" << std::endl;
    *out << "<Param enum_val0=\"X axis\" enum_val1=\"Y axis\" enum_cardinality=\"4\" enum_val2=\"Z axis\" enum_val3=\"custom axis\" type=\"RichEnum\" value=\"1\" name=\"rotAxis\"/>" << std::endl;
    *out << "<Param enum_val0=\"origin\" enum_val1=\"barycenter\" enum_cardinality=\"3\" enum_val2=\"custom point\" type=\"RichEnum\" value=\"0\" name=\"rotCenter\"/>" << std::endl;
    *out << "<Param type=\"RichDynamicFloat\" value=\""<< yRot <<"\" min=\"-360\" name=\"angle\" max=\"360\"/>" << std::endl;
    *out << "<Param type=\"RichBool\" value=\"false\" name=\"snapFlag\"/>" << std::endl;
    *out << "<Param x=\"0\" y=\"0\" z=\"0\" type=\"RichPoint3f\" name=\"customAxis\"/>" << std::endl;
    *out << "<Param x=\"0\" y=\"0\" z=\"0\" type=\"RichPoint3f\" name=\"customCenter\"/>" << std::endl;
    *out << "<Param type=\"RichFloat\" value=\"30\" name=\"snapAngle\"/>" << std::endl;
    *out << "<Param type=\"RichBool\" value=\"true\" name=\"Freeze\"/>" << std::endl;
    *out << "<Param type=\"RichBool\" value=\"false\" name=\"ToAll\"/>" << std::endl;
    *out << "</filter>" << std::endl;
    
    
    *out << "<filter name=\"Transform: Rotate\">" << std::endl;
    *out << "<Param enum_val0=\"X axis\" enum_val1=\"Y axis\" enum_cardinality=\"4\" enum_val2=\"Z axis\" enum_val3=\"custom axis\" type=\"RichEnum\" value=\"0\" name=\"rotAxis\"/>" << std::endl;
    *out << "<Param enum_val0=\"origin\" enum_val1=\"barycenter\" enum_cardinality=\"3\" enum_val2=\"custom point\" type=\"RichEnum\" value=\"0\" name=\"rotCenter\"/>" << std::endl;
    *out << "<Param type=\"RichDynamicFloat\" value=\""<< xRot <<"\" min=\"-360\" name=\"angle\" max=\"360\"/>" << std::endl;
    *out << "<Param type=\"RichBool\" value=\"false\" name=\"snapFlag\"/>" << std::endl;
    *out << "<Param x=\"0\" y=\"0\" z=\"0\" type=\"RichPoint3f\" name=\"customAxis\"/>" << std::endl;
    *out << "<Param x=\"0\" y=\"0\" z=\"0\" type=\"RichPoint3f\" name=\"customCenter\"/>" << std::endl;
    *out << "<Param type=\"RichFloat\" value=\"30\" name=\"snapAngle\"/>" << std::endl;
    *out << "<Param type=\"RichBool\" value=\"true\" name=\"Freeze\"/>" << std::endl;
    *out << "<Param type=\"RichBool\" value=\"false\" name=\"ToAll\"/>" << std::endl;
    *out << "</filter>" << std::endl;
    
    
    *out << "<filter name=\"Transform: Rotate\">" << std::endl;
    *out << "<Param enum_val0=\"X axis\" enum_val1=\"Y axis\" enum_cardinality=\"4\" enum_val2=\"Z axis\" enum_val3=\"custom axis\" type=\"RichEnum\" value=\"1\" name=\"rotAxis\"/>" << std::endl;
    *out << "<Param enum_val0=\"origin\" enum_val1=\"barycenter\" enum_cardinality=\"3\" enum_val2=\"custom point\" type=\"RichEnum\" value=\"0\" name=\"rotCenter\"/>" << std::endl;
    *out << "<Param type=\"RichDynamicFloat\" value=\""<< -90 <<"\" min=\"-360\" name=\"angle\" max=\"360\"/>" << std::endl;
    *out << "<Param type=\"RichBool\" value=\"false\" name=\"snapFlag\"/>" << std::endl;
    *out << "<Param x=\"0\" y=\"0\" z=\"0\" type=\"RichPoint3f\" name=\"customAxis\"/>" << std::endl;
    *out << "<Param x=\"0\" y=\"0\" z=\"0\" type=\"RichPoint3f\" name=\"customCenter\"/>" << std::endl;
    *out << "<Param type=\"RichFloat\" value=\"30\" name=\"snapAngle\"/>" << std::endl;
    *out << "<Param type=\"RichBool\" value=\"true\" name=\"Freeze\"/>" << std::endl;
    *out << "<Param type=\"RichBool\" value=\"false\" name=\"ToAll\"/>" << std::endl;
    *out << "</filter>" << std::endl;
        
    *out << "</FilterScript>" << std::endl;
    
    out->close();
    
    setCurrentOutput();
    PRINT("Output rotate.mlx");
    clearCurrentOutput();
    
    QDir dir = QDir::current();
    QString dirFileName = dir.absolutePath();
    
    setCurrentOutput();
    PRINT(dirFileName.toStdString());
    clearCurrentOutput();
    
    // Runmeshlab script to rotate target mesh
    std::stringstream command;
    //command << "/Applications/meshlabPatched.app/Contents/MacOS/meshlabserver -i " << targetFileName.toStdString() <<" -o rotatedMesh.stl -s rotate.mlx";
    command << dirFileName.toStdString() << "/../libraries/MeshLabMac_v132patched/meshlab.app/Contents/MacOS/meshlabserver -i " << targetFileName.toStdString() <<" -o rotatedMesh.stl -s rotate.mlx";
    
    const std::string tmp = command.str();
    const char* cstr = tmp.c_str();
    
    int rv = system(cstr);
    
    setCurrentOutput();
    PRINT(tmp);
    clearCurrentOutput();
    
    // remove previous binvox file

    if(std::remove("rotatedMesh.binvox") != 0)
      std::perror("No previous output to clear");
    else
      std::puts("Cleared previous output");

    // run binvox on rotated mesh to voxelize
    
    std::stringstream binVoxCommand;
    binVoxCommand<< dirFileName.toStdString() << "/../libraries/binvox/binvox  64 -c rotatedMesh.stl ";

    const std::string tmp2 = binVoxCommand.str();
    const char* cstr2 = tmp2.c_str();
    
    int rv2 = system(cstr2);
    
    //parse binvox file for scale and translate values
    const std::string binFileName = "rotatedMesh.binvox";
    readBinvoxScaleTranslate(binFileName);
    
    // run marchingCubes code to extrude part
    std::stringstream marchCommand;
    //marchCommand<< "/Users/follmer/Library/Developer/Xcode/DerivedData/glutMarch-dzjxobmlwtluttgyfdxwuivouinc/Build/Products/Debug/glutMarch rotatedMesh.binvox";
    marchCommand<< dirFileName.toStdString() << "/../glutMarch/Product/glutMarch rotatedMesh.binvox";
    
    const std::string tmp3 = marchCommand.str();
    const char* cstr3 = tmp3.c_str();
    
    int rv3 = system(cstr3);
    
    // reload that fhat file
    
    
    //trianglesExp.stl
    loadInsertedFiles();

}

void MainWindow::exportSTLSeanButtonAction(){
    setCurrentOutput();
    
	if (!this->root_N) {
		PRINT("Nothing to export! Try building first (press F6).");
		clearCurrentOutput();
		return;
	}
    
	if (this->root_N->dim != 3) {
		PRINT("Current top level object is not a 3D object.");
		clearCurrentOutput();
		return;
	}
    
	if (!this->root_N->p3->is_simple()) {
		PRINT("Object isn't a valid 2-manifold! Modify your design. See http://en.wikibooks.org/wiki/OpenSCAD_User_Manual/STL_Import_and_Export");
		clearCurrentOutput();
		return;
	}
    PRINT("CGAL Geometry exists and is manifold");
    
    QString suffix =  ".stl";
	QString stl_filename = "untitledMesh.stl";//QFileDialog::getSaveFileName(this, stl_mode ? "Export STL File" : "Export OFF File", this->fileName.isEmpty() ? "Untitled"+suffix : QFileInfo(this->fileName).baseName()+suffix, stl_mode ? "STL Files (*.stl)" : "OFF Files (*.off)");
	if (stl_filename.isEmpty()) {
		PRINT("No filename specified. export aborted.");
		clearCurrentOutput();
		return;
	}
    
	std::ofstream fstream(stl_filename.toUtf8());
	if (!fstream.is_open()) {
		PRINTB("Can't open file \"%s\" for export", stl_filename.toLocal8Bit().constData());
	}
	else {
		export_stl(this->root_N, fstream);
		
		fstream.close();
        
		PRINT("export finished.");
	}

    clearCurrentOutput();
    
}

void MainWindow::loadOriginalFiles()
{
    setCurrentOutput();
    PRINT("Hello World");
    clearCurrentOutput();
    /*
     setCurrentOutput();
     QTextEdit *e = new QTextEdit(this);
     e->setWindowFlags(Qt::Window);
     e->setTabStopWidth(30);
     e->setWindowTitle("Test");
     e->setReadOnly(true);
     if (root_module) {
     e->setPlainText(QString::fromLocal8Bit(root_module->dump("", "").c_str()));
     } else {
     e->setPlainText("No AST to dump. Please try compiling first...");
     }
     /*e->setPlainText("HelloWorld");
     e->show();
     e->resize(600, 400);
     clearCurrentOutput();*/
    
    const char *outfilename = "testOut";
    
    ModuleContext top_ctx;
	top_ctx.registerBuiltin();
    
	//FileModule *root_module;
	//ModuleInstantiation root_inst("group");
    
    delete this->root_module;
	this->root_module = NULL;
	
    std::stringstream parseCommand;
    parseCommand << "difference(){ {import(\"" ;
    parseCommand << enclosureFileName.toStdString();
    parseCommand << "\", convexity=2);} {import(\"";
    parseCommand << targetFileName.toStdString();
    parseCommand << "\", convexity=2);} };";
    
    const std::string tmp = parseCommand.str();
    const char* cstr = tmp.c_str();
	
	PRINT(tmp);
    
	this->root_module = parse(cstr,"",false);
    instantiateRoot();
    
    int numChildren = this->root_node->getChildren()[0]->getChildren()[1]->getChildren().size();
    std::stringstream ss1;
    ss1 << numChildren;
    std::string str1 = ss1.str();
    setCurrentOutput();
    PRINT("NUM CHILDREN");
    PRINT(str1);
    clearCurrentOutput();
    
    ModuleInstantiation * root_inst = new ModuleInstantiation("transform");
    /*root_inst->tag_root = false;
     root_inst->tag_highlight = false;
     root_inst->tag_background = false;*/
    
    TransformNode *transNode = new TransformNode(root_inst);
    transNode->matrix = Transform3d::Identity();
    transNode->matrix(2,3)=-5;
    
    ModuleInstantiation * root_inst2 = new ModuleInstantiation("transform");
    /*root_inst->tag_root = false;
     root_inst->tag_highlight = false;
     root_inst->tag_background = false;*/
    
    PrimitiveNode *node = new PrimitiveNode(root_inst2, CUBE);
    node->center = false;
	node->x = node->y = node->z = node->h = node->r1 = node->r2 = 1;
    transNode->children.push_back(node);
    
    //this->root_node->getChildren()[0]->getChildren()[1]->children.push_back(transNode);
    
    //std::vector<AbstractNode*>  children = this->root_node->getChildren()[1]->getChildren();
    //children.push_back(transNode);
    
    numChildren = this->root_node->getChildren()[0]->getChildren()[1]->getChildren().size();
    ss1 << numChildren;
    str1 = ss1.str();
    setCurrentOutput();
    PRINT("NUM CHILDREN");
    PRINT(str1);
    
    PRINT(this->tree.getString(*this->root_node));
    
    const std::string file = targetFileName.toStdString();
    setCurrentOutput();
    PRINT(file);
    
    clearCurrentOutput();
    
    csgRender();
    
}

void MainWindow::loadInsertedFiles() {
    setCurrentOutput();
    PRINT("Hello World");
    clearCurrentOutput();
    /*
     setCurrentOutput();
     QTextEdit *e = new QTextEdit(this);
     e->setWindowFlags(Qt::Window);
     e->setTabStopWidth(30);
     e->setWindowTitle("Test");
     e->setReadOnly(true);
     if (root_module) {
     e->setPlainText(QString::fromLocal8Bit(root_module->dump("", "").c_str()));
     } else {
     e->setPlainText("No AST to dump. Please try compiling first...");
     }
     /*e->setPlainText("HelloWorld");
     e->show();
     e->resize(600, 400);
     clearCurrentOutput();*/
    
    const char *outfilename = "testOut";
    
    ModuleContext top_ctx;
	top_ctx.registerBuiltin();
    
	//FileModule *root_module;
	//ModuleInstantiation root_inst("group");
    
    delete this->root_module;
	this->root_module = NULL;
	
    
    std::stringstream parseCommand;
    parseCommand << "difference(){ {import(\"" ;
    parseCommand << enclosureFileName.toStdString();
    parseCommand << "\", convexity=2);} { ";
    
    parseCommand << "rotate(a=" << -1*zRot <<", v=[0,0,1]) "; //invert insertion direction rotation
    parseCommand << "rotate(a=" << -1*yRot <<", v=[0,1,0]) ";
    parseCommand << "rotate(a=" << -1*xRot <<", v=[1,0,0]) ";
    
    
    parseCommand << "rotate(a=90, v=[0,1,0]) ";     //invert -90Y rot for binvox
    parseCommand << "translate([" << binvoxTransX << ", "<< binvoxTransY << ", "<< binvoxTransZ << "]) ";
    parseCommand << "scale([" << binvoxScale << ", "<< binvoxScale << ", "<< binvoxScale << "]) ";
    parseCommand << "rotate(a=90, v=[1,0,0]) rotate(a=90, v=[0,1,0] ) import(\"trianglesExp.stl\", convexity=2);} };"; //rotate to get back to correct position (I think due to a x,y,z read order error when importing binvox volume corrdinates to marching cubes app)
    
    setCurrentOutput();
    PRINT(parseCommand.str());
    clearCurrentOutput();
    /*
    std::stringstream parseCommand2;
    parseCommand2 << "difference(){ {import(\"" ;
    parseCommand2 << enclosureFileName.toStdString();
    parseCommand2 << "\", convexity=2);} {import(\"trianglesExp.stl\", convexity=2);} };"; */
    const std::string tmp = parseCommand.str();
    const char* cstr = tmp.c_str();
    
	this->root_module = parse(cstr,"",false);
    instantiateRoot();
    
        
    PRINT(this->tree.getString(*this->root_node));
    
    const std::string file = targetFileName.toStdString();
    setCurrentOutput();
    PRINT(file);
    
    clearCurrentOutput();
    
    csgRender();
    
    
    addCuttingPlaneAfter();
    
}

void MainWindow::loadInsertedFilesNoRender() {
    setCurrentOutput();
    PRINT("Hello World");
    clearCurrentOutput();
        
    const char *outfilename = "testOut";
    
    ModuleContext top_ctx;
	top_ctx.registerBuiltin();
    
	//FileModule *root_module;
	//ModuleInstantiation root_inst("group");
    
    delete this->root_module;
	this->root_module = NULL;
	
    
    std::stringstream parseCommand;
    parseCommand << "difference(){ {import(\"" ;
    parseCommand << enclosureFileName.toStdString();
    parseCommand << "\", convexity=2);} { ";
    
    parseCommand << "rotate(a=" << -1*zRot <<", v=[0,0,1]) "; //invert insertion direction rotation
    parseCommand << "rotate(a=" << -1*yRot <<", v=[0,1,0]) ";
    parseCommand << "rotate(a=" << -1*xRot <<", v=[1,0,0]) ";
    
    
    parseCommand << "rotate(a=90, v=[0,1,0]) ";     //invert -90Y rot for binvox
    parseCommand << "translate([" << binvoxTransX << ", "<< binvoxTransY << ", "<< binvoxTransZ << "]) ";
    parseCommand << "scale([" << binvoxScale << ", "<< binvoxScale << ", "<< binvoxScale << "]) ";
    parseCommand << "rotate(a=90, v=[1,0,0]) rotate(a=90, v=[0,1,0] ) import(\"trianglesExp.stl\", convexity=2);} };"; //rotate to get back to correct position (I think due to a x,y,z read order error when importing binvox volume corrdinates to marching cubes app)
    
    setCurrentOutput();
    PRINT(parseCommand.str());
    clearCurrentOutput();
    /*
     std::stringstream parseCommand2;
     parseCommand2 << "difference(){ {import(\"" ;
     parseCommand2 << enclosureFileName.toStdString();
     parseCommand2 << "\", convexity=2);} {import(\"trianglesExp.stl\", convexity=2);} };"; */
    const std::string tmp = parseCommand.str();
    const char* cstr = tmp.c_str();
    
	this->root_module = parse(cstr,"",false);
    instantiateRoot();
    
    
    PRINT(this->tree.getString(*this->root_node));
    
    const std::string file = targetFileName.toStdString();
    setCurrentOutput();
    PRINT(file);
    
    clearCurrentOutput();
    
  
    
    
    addCuttingPlaneAfterNoRender();
    
}

void MainWindow::readBinvoxScaleTranslate(std::string filespec){
    std::ifstream *input = new std::ifstream(filespec.c_str());
    
    //
    // read header
    //
    std::string line;
    *input >> line;  // #binvox
    if(line.length()==0){
        //cout << "no first line" <<endl;
    }
    if (line.compare("#binvox") != 0) {
        //cout << "Error: first line reads [" << line << "] instead of [#binvox]" << endl;
        delete input;
        return;
    }
    int version;
    *input >> version;
    //cout << "reading binvox version " << version << endl;
    
    int depth, height, width;
    depth = -1;
    int done = 0;
    while(input->good() && !done) {
        *input >> line;
        if (line.compare("data") == 0) done = 1;
        else if (line.compare("dim") == 0) {
            *input >> depth >> height >> width;
        }
        else if (line.compare("translate") == 0) {
            *input >> binvoxTransX >> binvoxTransY >> binvoxTransZ;
        }
        else if (line.compare("scale") == 0) {
            *input >> binvoxScale;
        }
        else {
            //cout << "  unrecognized keyword [" << line << "], skipping" << endl;
            char c;
            do {  // skip until end of line
                c = input->get();
            } while(input->good() && (c != '\n'));
            
        }
    }
    
    setCurrentOutput();
    PRINT("Binvox file values");
    PRINTB("%f",binvoxTransX);
    PRINTB("%f",binvoxTransY);
    PRINTB("%f",binvoxTransZ);
    PRINTB("%f",binvoxScale);
    
    clearCurrentOutput();
    
    if (!done) {
        //cout << "  error reading header" << endl;
        return;
    }
    if (depth == -1) {
        //cout << "  missing dimensions in header" << endl;
        return;
    }
    
   

    
}

void MainWindow::computeScrewPositionsAction(){
    crossSection();
}

void MainWindow::crossSection(){
    
    crossSectionMode = true;
    
    crossSectionModel(targetFileName.toStdString());
}

void MainWindow::crossSectionOutLines(){
    setCurrentOutput();
    PRINT("Done rendering cgal");
    clearCurrentOutput();   
    if(crossSectionModelNumber==0){
        crossSectionTarget = this->root_N;
        this->root_N =NULL;
        crossSectionModel(enclosureFileName.toStdString());
    }
    if (crossSectionModelNumber==1){
        crossSectionEnclosure = this->root_N;
        this->root_N =NULL;
        

        
        crossSectionModelBinvox();
    }
    if (crossSectionModelNumber==2){
        crossSectionTargetInserted= this->root_N;
        this->root_N =NULL;
    
        CGAL_Nef_polyhedron crossSectionEncMinInserted = (crossSectionEnclosure->copy());
        crossSectionEncMinInserted-= (*crossSectionTargetInserted);
        //(*crossSectionEnclosure)-=  (*crossSectionTarget);
        
        //cavityPointsTarget = findCloseCavityPoints(crossSectionTarget, crossSectionEnclosure);
        cavityPointsInserted = findCloseCavityPoints(crossSectionTargetInserted, &crossSectionEncMinInserted);
        
        setCurrentOutput();
        PRINTB("cavity points target %d", (cavityPointsTarget.size()));
                clearCurrentOutput();
        
        std::stringstream parseCommand;
        for(int i =0; i < cavityPointsTarget.size(); i++){
            parseCommand<<cavityPointsTarget[i].first<<", "<<cavityPointsTarget[i].second<<std::endl;
        }
        
        setCurrentOutput();
        PRINT(parseCommand.str());
        clearCurrentOutput();
       
        setCurrentOutput();
        PRINTB("cavity points inserted %d", (cavityPointsInserted.size()));
        clearCurrentOutput();
        
        std::stringstream parseCommand2;
        for(int i =0; i < cavityPointsInserted.size(); i++){
            parseCommand2<<cavityPointsInserted[i].first<<", "<<cavityPointsInserted[i].second<<std::endl;
        }
        
        setCurrentOutput();
        PRINT(parseCommand2.str());
        clearCurrentOutput();
        
        //now find screw positions inside crossSectionEncMinInserted
        
        DxfData *dd =crossSectionEncMinInserted.convertToDxfData();
        findScrewPositions(dd);
        
        
        crossSectionMode = false;
    }
    
    
    crossSectionModelNumber++;
}

void MainWindow::crossSectionModel(std::string modelFileName){
    
    Transform3d transMatrix2 = cuttingPlaneMatrixRot.inverse();
    Transform3d transMatrix90Deg = Transform3d::Identity();
    transMatrix90Deg(1,1)=cos(90*3.14159/180.0);
    transMatrix90Deg(1,2)=-1*sin(90*3.14159/180.0);
    transMatrix90Deg(2,1)=sin(90*3.14159/180.0);
    transMatrix90Deg(2,2)=cos(90*3.14159/180.0);
    transMatrix2 = transMatrix90Deg * transMatrix2 ;
    
    transMatrixInverse = transMatrix2.inverse();
    
    delete this->root_module;
	this->root_module = NULL;
	
    
    std::stringstream parseCommand;
    parseCommand << "projection(cut = true){ {";
    
    parseCommand <<"multmatrix(m = [ [" << transMatrix2(0,0) << " , " << transMatrix2(0,1) << " , " << transMatrix2(0,2) << " , " << transMatrix2(0,3) << " ], ";
    parseCommand <<"[ " << transMatrix2(1,0) << " , " << transMatrix2(1,1) << " , " << transMatrix2(1,2) << " , " << transMatrix2(1,3) << " ], ";
    parseCommand <<"[ " << transMatrix2(2,0) << " , " << transMatrix2(2,1) << " , " << transMatrix2(2,2) << " , " << transMatrix2(2,3) << " ], ";
    parseCommand <<"[ " << transMatrix2(3,0) << " , " << transMatrix2(3,1) << " , " << transMatrix2(3,2) << " , " << transMatrix2(3,3) << " ] ] ) ";
    parseCommand << "import(\"" ;
    parseCommand << modelFileName;
    parseCommand << "\", convexity=2);} } ";
    
    
    setCurrentOutput();
    PRINT(parseCommand.str());
    clearCurrentOutput();
    
    const std::string tmp = parseCommand.str();
    const char* cstr = tmp.c_str();
    
	this->root_module = parse(cstr,"",false);
    instantiateRoot();
    
    
    PRINT(this->tree.getString(*this->root_node));
    
    cgalRender();
    
}

QVector<Point2D> MainWindow::findCloseCavityPoints(class CGAL_Nef_polyhedron *target, class CGAL_Nef_polyhedron *enclosure){
    
    
     
     DxfData *dd =enclosure->convertToDxfData();
     DxfData *ddTarget =target->convertToDxfData();
    QVector<Point2D> closePoints;
    
     for (size_t i=0; i<dd->paths.size(); i++) {
         for (size_t j=0; j<dd->paths[i].indices.size(); j++) {
     
             const Vector2d &p2 = dd->points[dd->paths[i].indices[j]];
     
             double x2 = p2[0];
             double y2 = p2[1];
             
             for (size_t i2=0; i2<ddTarget->paths.size(); i2++) {
                 for (size_t j2=0; j2<ddTarget->paths[i2].indices.size(); j2++) {
                     const Vector2d &p3 = ddTarget->points[ddTarget->paths[i2].indices[j2]];
                     
                     double x3 = p3[0];
                     double y3 = p3[1];
                     
                     if((x2==x3)&&(y2==y3)){
                     
                        /* setCurrentOutput();
                         PRINT("SAME Point found");
                         PRINTB("%f x pt", x2);
                         PRINTB("%f y pt", y2);
                         clearCurrentOutput();*/
                         
                         closePoints.push_back(Point2D(x2,y2));
                         
                     }
                 }
             }
         }
     
     }
    return closePoints;
    
    
}

void MainWindow::crossSectionModelBinvox(){
    
    Transform3d transMatrix2 = cuttingPlaneMatrixRot.inverse();
    Transform3d transMatrix90Deg = Transform3d::Identity();
    transMatrix90Deg(1,1)=cos(90*3.14159/180.0);
    transMatrix90Deg(1,2)=-1*sin(90*3.14159/180.0);
    transMatrix90Deg(2,1)=sin(90*3.14159/180.0);
    transMatrix90Deg(2,2)=cos(90*3.14159/180.0);
    transMatrix2 = transMatrix90Deg * transMatrix2 ;
    
    delete this->root_module;
	this->root_module = NULL;
    
    
    std::stringstream parseCommand;
    parseCommand << "projection(cut = true){ {";
    
    parseCommand <<"multmatrix(m = [ [" << transMatrix2(0,0) << " , " << transMatrix2(0,1) << " , " << transMatrix2(0,2) << " , " << transMatrix2(0,3) << " ], ";
    parseCommand <<"[ " << transMatrix2(1,0) << " , " << transMatrix2(1,1) << " , " << transMatrix2(1,2) << " , " << transMatrix2(1,3) << " ], ";
    parseCommand <<"[ " << transMatrix2(2,0) << " , " << transMatrix2(2,1) << " , " << transMatrix2(2,2) << " , " << transMatrix2(2,3) << " ], ";
    parseCommand <<"[ " << transMatrix2(3,0) << " , " << transMatrix2(3,1) << " , " << transMatrix2(3,2) << " , " << transMatrix2(3,3) << " ] ] ) ";
    
    parseCommand << "rotate(a=" << -1*zRot <<", v=[0,0,1]) "; //invert insertion direction rotation
    parseCommand << "rotate(a=" << -1*yRot <<", v=[0,1,0]) ";
    parseCommand << "rotate(a=" << -1*xRot <<", v=[1,0,0]) ";
    
    
    parseCommand << "rotate(a=90, v=[0,1,0]) ";     //invert -90Y rot for binvox
    parseCommand << "translate([" << binvoxTransX << ", "<< binvoxTransY << ", "<< binvoxTransZ << "]) ";
    parseCommand << "scale([" << binvoxScale << ", "<< binvoxScale << ", "<< binvoxScale << "]) ";
    parseCommand << "rotate(a=90, v=[1,0,0]) rotate(a=90, v=[0,1,0] ) import(\"trianglesExp.stl\", convexity=2);}} ";
    
    setCurrentOutput();
    PRINT(parseCommand.str());
    clearCurrentOutput();
    
    const std::string tmp = parseCommand.str();
    const char* cstr = tmp.c_str();
    
	this->root_module = parse(cstr,"",false);
    instantiateRoot();
    
    
    PRINT(this->tree.getString(*this->root_node));
    
    cgalRender();


    
}

void MainWindow::findScrewPositions(DxfData *dd)
{
    std::srand(std::time(NULL));
    
    if( (dd->paths.size()) ==1){
     
        //bounding box
        double maxX, minX, maxY, minY;
        
        setCurrentOutput();
        PRINTB("size of indices %f", (dd->paths[0].indices.size()));
        clearCurrentOutput();
        
        
        const Vector2d &p1 = dd->points[dd->paths[0].indices[0]];
        
        maxX = p1[0];
        minX = p1[0];
        maxY = p1[1];
        minY = p1[1];
        
        
        
        for (size_t j=0; j<dd->paths[0].indices.size(); j++) {
            const Vector2d &p2 = dd->points[dd->paths[0].indices[j]];
            
            double x2 = p2[0];
            double y2 = p2[1];
            
            if(x2>maxX) maxX = x2;
            else if (x2<minX) minX = x2;
            
            if (y2>maxY) maxY = y2;
            else if (y2<minY) minY = y2;
            
        } 
        
        // now have max and min
        
       
        
        int x0 = minX;
        int xDist = int(maxX-minX) + 1;
        int y0 = minY;
        int yDist = int(maxY-minY) + 1;
        int gridSpacing = 5;
        
        
        setCurrentOutput();
        PRINTB("MinX %d", minX);
        PRINTB("MinY %d", minY);
        PRINTB("dist x %d ", xDist);
        PRINTB("dist y %d", yDist);
        clearCurrentOutput();
        
        
        gridCell *grid = new gridCell[gridSpacing*xDist*gridSpacing*yDist];
        Vector2d screw1Pos; //= picRandomPointInPolygon(dd, minX,  minY,  yDist,  xDist);
        Vector2d screw2Pos; //= picRandomPointInPolygon(dd, minX,  minY,  yDist,  xDist);
        Vector2d screw3Pos; //= picRandomPointInPolygon(dd, minX,  minY,  yDist,  xDist);
        
        
        int countInside=0;
        int countEdgeCells=0;
        double currentEnergySum =0;
        for(int i=0; i < (gridSpacing*xDist); i++){
            for(int j=0; j < (gridSpacing*yDist); j++){
                
                
                grid[(i*(gridSpacing*xDist)+j)].xLoc = double(i) / double(gridSpacing*xDist) * double(xDist) + double(minX);
                grid[(i*(gridSpacing*xDist)+j)].yLoc = double(j) / double(gridSpacing*yDist) * double(yDist) + double(minY);
                if (pnpoly( dd, grid[(i*(gridSpacing*xDist)+j)].xLoc, grid[(i*(gridSpacing*xDist)+j)].yLoc)==1) {
                    grid[(i*(gridSpacing*xDist)+j)].insidePoly = true;
                    countInside++;
                    // find out if near edge
                    if (testIfCloseToPolygonEdge(dd, grid[(i*(gridSpacing*xDist)+j)].xLoc, grid[(i*(gridSpacing*xDist)+j)].yLoc, 5 )){ // dist in mm
                        grid[(i*(gridSpacing*xDist)+j)].nearEdge = true;
                        countEdgeCells++;
                    }
                    //
                    /*double tempEnergy = distanceToPoint(grid[(i*(gridSpacing*xDist)+j)].xLoc, grid[(i*(gridSpacing*xDist)+j)].yLoc, screw1Pos[0], screw1Pos[1] );
                    tempEnergy += distanceToPoint(grid[(i*(gridSpacing*xDist)+j)].xLoc, grid[(i*(gridSpacing*xDist)+j)].yLoc, screw2Pos[0], screw2Pos[1] );
                    tempEnergy += distanceToPoint(grid[(i*(gridSpacing*xDist)+j)].xLoc, grid[(i*(gridSpacing*xDist)+j)].yLoc, screw3Pos[0], screw3Pos[1] );
                    grid[(i*(gridSpacing*xDist)+j)].currentEnergy = tempEnergy;
                    currentEnergySum+= tempEnergy;*/
                }
                else{
                    grid[(i*(gridSpacing*xDist)+j)].insidePoly = false;
                }
            }
        }
        
        //pic random positions in grid that are good
        int screwIndex1;
        int screwIndex2;
        int screwIndex3;
        int tempCount = 0;
        screwIndex1 = pickRandomGoodGridCell(grid, gridSpacing*xDist*gridSpacing*yDist);
        screw1Pos = Vector2d(grid[screwIndex1].xLoc, grid[screwIndex1].yLoc);

        
        /*
        while (tempCount<1000) {
            screwIndex1 = pickRandomGoodGridCell(grid, gridSpacing*xDist*gridSpacing*yDist);
            screw1Pos = Vector2d(grid[screwIndex1].xLoc, grid[screwIndex1].yLoc);
            if( (screw1Pos[1] < (minY+ (yDist/3.0)))) break;
        }
        while (tempCount<1000) {
            screwIndex2 = pickRandomGoodGridCell(grid, gridSpacing*xDist*gridSpacing*yDist);
            screw2Pos = Vector2d(grid[screwIndex2].xLoc, grid[screwIndex2].yLoc);

            if( (screw2Pos[1] > (minY+ (yDist/3.0))) && (screw2Pos[1] < (minY+ (yDist/3.0)*2))) break;
        }
        while (tempCount<1000) {
            screwIndex3 = pickRandomGoodGridCell(grid, gridSpacing*xDist*gridSpacing*yDist);
            screw3Pos = Vector2d(grid[screwIndex3].xLoc, grid[screwIndex3].yLoc);
                     
            if( (screw3Pos[1] > (minY+ (yDist/3.0)*2) )) break;
        } */
        
        while (tempCount<1000) {
            tempCount++;
            screwIndex2 = pickRandomGoodGridCell(grid, gridSpacing*xDist*gridSpacing*yDist);
            screw2Pos = Vector2d(grid[screwIndex2].xLoc, grid[screwIndex2].yLoc);
            screwIndex3 = pickRandomGoodGridCell(grid, gridSpacing*xDist*gridSpacing*yDist);
            screw3Pos = Vector2d(grid[screwIndex3].xLoc, grid[screwIndex3].yLoc);
            
            if( (distanceToPoint(screw1Pos[0], screw1Pos[1], screw2Pos[0], screw2Pos[1] ) > 30 ) && (distanceToPoint(screw1Pos[0], screw1Pos[1], screw3Pos[0], screw3Pos[1] ) > 30 ) && (distanceToPoint(screw2Pos[0], screw2Pos[1], screw3Pos[0], screw3Pos[1] ) > 30 )) {
                setCurrentOutput();
                PRINT("Found one good start");
                break;
            }
        }
        
        
        
        setCurrentOutput();
        PRINTB("random in poly x %f", screw1Pos[0]);
        PRINTB("random in poly y %f", screw1Pos[1]);
        PRINTB("random in poly x 2 %f", screw2Pos[0]);
        PRINTB("random in poly y 2 %f", screw2Pos[1]);
        PRINTB("random in poly x 3 %f", screw3Pos[0]);
        PRINTB("random in poly y 3 %f", screw3Pos[1]);
        
        clearCurrentOutput();
        
        
        // calculate current energy
        currentEnergySum =0;
        
        
        for(int i =0; i < ( gridSpacing*xDist*gridSpacing*yDist); i ++) {
            
            if( (grid[i].insidePoly ) && ! (grid[i].nearEdge ) ){
                 double tempEnergy = distanceToPoint(grid[i].xLoc, grid[i].yLoc, screw1Pos[0], screw1Pos[1] );
                 tempEnergy += distanceToPoint(grid[i].xLoc, grid[i].yLoc, screw2Pos[0], screw2Pos[1] );
                 tempEnergy += distanceToPoint(grid[i].xLoc, grid[i].yLoc, screw3Pos[0], screw3Pos[1] );
                 grid[i].currentEnergy = tempEnergy;
                
                 currentEnergySum+= tempEnergy;
            }
            
        }
        
        int numPossibleMoves = 12;
        double possilbeEnergies [numPossibleMoves];
        for (int i = 0; i < numPossibleMoves; i++) possilbeEnergies[i]=0;
        Vector2d * possiblePositions = new Vector2d [numPossibleMoves];
        double possiblePositionsX [numPossibleMoves];
        double possiblePositionsY [numPossibleMoves];
        int possiblePositionIndicies [numPossibleMoves];
        bool isOkMove [numPossibleMoves];
        
        for (int k = 0; k < 1000; k++) {
            
            for (int i = 0; i < numPossibleMoves; i++){
                possilbeEnergies[i]=0;
                isOkMove[i]=true;
            }
            currentEnergySum=0;
            
            
            int stepSize = 1;
            // find positions of next points
            //screw 1
            possiblePositionIndicies[0] = screwIndex1-stepSize;
            possiblePositionIndicies[1] = screwIndex1+stepSize;
            possiblePositionIndicies[2] = screwIndex1- (gridSpacing*xDist*stepSize);
            possiblePositionIndicies[3] = screwIndex1+ (gridSpacing*xDist*stepSize);
            // screw 2
            possiblePositionIndicies[4] = screwIndex2-stepSize;
            possiblePositionIndicies[5] = screwIndex2+stepSize;
            possiblePositionIndicies[6] = screwIndex2- (gridSpacing*xDist* stepSize);
            possiblePositionIndicies[7] = screwIndex2+ (gridSpacing*xDist*stepSize);
            //screw 3
            possiblePositionIndicies[8] = screwIndex3-stepSize;
            possiblePositionIndicies[9] = screwIndex3+stepSize;
            possiblePositionIndicies[10] = screwIndex3- (gridSpacing*xDist*stepSize);
            possiblePositionIndicies[11] = screwIndex3+ (gridSpacing*xDist*stepSize);
            
            
            for(int h=0; h < numPossibleMoves; h++){
                possiblePositions[h]=Vector2d(grid[possiblePositionIndicies[h]].xLoc, grid[possiblePositionIndicies[h]].yLoc);
                possiblePositionsX[h] = grid[possiblePositionIndicies[h]].xLoc;
                possiblePositionsY[h] = grid[possiblePositionIndicies[h]].yLoc;
                
                if((grid[possiblePositionIndicies[h]].insidePoly ) && !(grid[possiblePositionIndicies[h]].nearEdge)){
                    isOkMove[h] = true;
                }
                else{
                    isOkMove[h] = false;
                    
                }
            }
            
            // calculate energy
            for(int i =0; i < ( gridSpacing*xDist*gridSpacing*yDist); i ++) {
                
                if( (grid[i].insidePoly )  ){
                    double tempEnergy = distanceToPoint(grid[i].xLoc, grid[i].yLoc, screw1Pos[0], screw1Pos[1] );
                    tempEnergy += distanceToPoint(grid[i].xLoc, grid[i].yLoc, screw2Pos[0], screw2Pos[1] );
                    tempEnergy += distanceToPoint(grid[i].xLoc, grid[i].yLoc, screw3Pos[0], screw3Pos[1] );
                    grid[i].currentEnergy = tempEnergy;
                    
                    currentEnergySum+= tempEnergy;
                    
                    
                    for(int h=0; h < 4; h++){
                        if(isOkMove[h]) {
                        
                            grid[i].possibleEnergy[h] = distanceToPoint(grid[i].xLoc, grid[i].yLoc, possiblePositionsX[h], possiblePositionsY[h] );
                            grid[i].possibleEnergy[h] += distanceToPoint(grid[i].xLoc, grid[i].yLoc, screw2Pos[0], screw2Pos[1] );
                            grid[i].possibleEnergy[h] += distanceToPoint(grid[i].xLoc, grid[i].yLoc, screw3Pos[0], screw3Pos[1] );
                        
                            possilbeEnergies[h]+= grid[i].possibleEnergy[h];
                        }
                    }
                    for(int h=4; h < 8; h++){
                        
                        if(isOkMove[h]) {
                            grid[i].possibleEnergy[h] = distanceToPoint(grid[i].xLoc, grid[i].yLoc, screw1Pos[0], screw1Pos[1] );
                            grid[i].possibleEnergy[h] += distanceToPoint(grid[i].xLoc, grid[i].yLoc, possiblePositionsX[h], possiblePositionsY[h] );
                            grid[i].possibleEnergy[h] += distanceToPoint(grid[i].xLoc, grid[i].yLoc, screw3Pos[0], screw3Pos[1] );
                        
                            possilbeEnergies[h]+= grid[i].possibleEnergy[h];
                        }
                    }
                    for(int h=8; h < 12; h++){
                        
                        if(isOkMove[h]) {
                            grid[i].possibleEnergy[h] = distanceToPoint(grid[i].xLoc, grid[i].yLoc, screw1Pos[0], screw1Pos[1] );
                            grid[i].possibleEnergy[h] += distanceToPoint(grid[i].xLoc, grid[i].yLoc, screw2Pos[0], screw2Pos[1] );
                            grid[i].possibleEnergy[h] += distanceToPoint(grid[i].xLoc, grid[i].yLoc, possiblePositionsX[h], possiblePositionsY[h]);
                        
                            possilbeEnergies[h]+= grid[i].possibleEnergy[h];
                        }
                    }
                    
                }
                
                
                
            }
            
            setCurrentOutput();
            PRINTB("current energies %f", currentEnergySum);
            
            for(int h=0; h < numPossibleMoves; h++) PRINTB("possible energies %f", possilbeEnergies[h]);
            clearCurrentOutput();
            // move based on least energy
            
            int currentSmallestIndex = -1;
            double currentSmallestEnergy = currentEnergySum;
            for(int h=0; h < numPossibleMoves; h++){
                if(isOkMove[h]) {
                    if(possilbeEnergies[h]<currentSmallestEnergy){
                        currentSmallestEnergy= possilbeEnergies[h];
                        currentSmallestIndex =h;
                    }
                }
                
            }
            
            if (currentSmallestIndex != -1){
                if(currentSmallestIndex<4){
                    screw1Pos[0] = possiblePositionsX[currentSmallestIndex];
                    screw1Pos[1] = possiblePositionsY[currentSmallestIndex];
                    screwIndex1 = possiblePositionIndicies[currentSmallestIndex];
                }else if (currentSmallestIndex<8){
                    screw2Pos[0] = possiblePositionsX[currentSmallestIndex];
                    screw2Pos[1] = possiblePositionsY[currentSmallestIndex];
                    screwIndex3 = possiblePositionIndicies[currentSmallestIndex];
                } else if (currentSmallestIndex<12 ) {
                    screw3Pos[0] = possiblePositionsX[currentSmallestIndex];
                    screw3Pos[1] = possiblePositionsY[currentSmallestIndex];
                    screwIndex3 = possiblePositionIndicies[currentSmallestIndex];
                }
                
                
            }
            
            setCurrentOutput();
            PRINTB("current smallest index %d", currentSmallestIndex);
            PRINTB("screw1 x %f", screw1Pos[0]);
            PRINTB("screw1 y %f", screw1Pos[1]);
            PRINTB("screw x 2 %f", screw2Pos[0]);
            PRINTB("screw y 2 %f", screw2Pos[1]);
            PRINTB("screw x 3 %f", screw3Pos[0]);
            PRINTB("screw y 3 %f", screw3Pos[1]);
            
            clearCurrentOutput();
            
            
            
            
        }
        
        
        //calculate energy of 12 possible moves
        
        
        
        
        
        
        
        
        
        
        setCurrentOutput();
        PRINTB("num pixels inside poly %d", countInside);
        PRINTB("num edge cells %d", countEdgeCells);
        PRINTB("num pixels %d", (gridSpacing*xDist*gridSpacing*yDist) );
        PRINTB("current energy %f",currentEnergySum);
        clearCurrentOutput();
        
        /*loadInsertedFilesNoRender();
        addScrewHole(screw1Pos[0], screw1Pos[1]);
        addScrewHole(screw2Pos[0], screw2Pos[1]);
        addScrewHole(screw3Pos[0], screw3Pos[1]);
        csgRender();*/
        //testDrawScrewPos(screw1Pos[0], screw1Pos[1]);
        
        
        screw1X = screw1Pos[0];
        screw1Y = screw1Pos[1];
        screw2X = screw2Pos[0];
        screw2Y = screw2Pos[1];
        screw3X = screw3Pos[0];
        screw3Y =  screw3Pos[1];
        testDrawScrewPosAssembly();
    }
    
    
}

Vector2d MainWindow::picRandomPointInPolygon(DxfData *dd, int minX, int minY, int yDist, int xDist){
    
    int aRand = std::rand();
    double dRand= aRand / 2147483647.0;
    setCurrentOutput();
    PRINTB("random num %d", aRand);
    PRINTB("random num float %f", dRand);
    clearCurrentOutput(); 
    
    
    
    
    double xRand = double(std::rand()/2147483647.0) * double(xDist) + double(minX);
    double yRand = double(std::rand()/2147483647.0) * double(yDist) + double(minY);
    
    while (true) {
        if ( pnpoly(dd, xRand, yRand) == 1) 
            break;
        
        else {
            xRand = double(std::rand()/2147483647.0) * double(xDist) + double(minX);
            yRand = double(std::rand()/2147483647.0) * double(yDist) + double(minY);
        }
    }
    Vector2d  vec = Vector2d(xRand, yRand);
    return vec;
}

int MainWindow::pnpoly( DxfData *dd, double testx, double testy)
{
    
    int i, j, c = 0;
    if( (dd->paths.size()) ==1){
        for (i = 0, j = (dd->paths[0].indices.size())-1; i < (dd->paths[0].indices.size()); j = i++) {
            const Vector2d &p1 = dd->points[dd->paths[0].indices[i]];
            
            double x1 = p1[0];
            double y1 = p1[1];
            
            const Vector2d &p2 = dd->points[dd->paths[0].indices[j]];
            
            double x2 = p2[0];
            double y2 = p2[1];
            
            
            if ( ((y1>testy) != (y2>testy)) &&
                (testx < (x2-x1) * (testy-y1) / (y2-y1) + x1) )
                    c = !c;
        }
    }
    return c;
    
}

double MainWindow::distanceToPoint(double x1, double y1, double x2, double y2 ){
    
    return sqrt((x2-x1)*(x2-x1)+(y2-y1)*(y2-y1));
    
}



double MainWindow::distToLineSegment(double x1, double y1, double x2, double y2, double x3, double y3){ //x3,y3 is the point
    double px = x2-x1;
    double py = y2-y1;
 
    double something = px*px + py*py;
 
    double u =  ((x3 - x1) * px + (y3 - y1) * py) / (something);
 
    if (u > 1) u = 1;
    else if( u < 0) u = 0;
 
    double x = x1 + u * px;
    double y = y1 + u * py;
 
    double dx = x - x3;
    double dy = y - y3;
 
 /*# Note: If the actual distance does not matter,
 # if you only want to compare what this function
 # returns to other results of this function, you
 # can just return the squared distance instead
 # (i.e. remove the sqrt) to gain a little performance*/
 
    double dist = sqrt(dx*dx + dy*dy);
 
    return dist;

}

bool MainWindow::testIfCloseToPolygonEdge(DxfData *dd, double testx, double testy, double minDist){
    bool isTooClose = false;
    if( (dd->paths.size()) ==1){
        int i, j =0;
        for ( i = 0,  j = (dd->paths[0].indices.size())-1; i < (dd->paths[0].indices.size()); j = i++) {
            const Vector2d &p1 = dd->points[dd->paths[0].indices[i]];
            
            double x1 = p1[0];
            double y1 = p1[1];
            
            const Vector2d &p2 = dd->points[dd->paths[0].indices[j]];
            
            double x2 = p2[0];
            double y2 = p2[1];

            double dist = distToLineSegment(x1,  y1,  x2,  y2,  testx, testy );
            if( dist < minDist) {
                // too close
                isTooClose = true;
                return isTooClose;
            }
            
        }
    }
    
    return isTooClose;
}

int MainWindow::pickRandomGoodGridCell(gridCell* grid, int gridSize){
    while(true){
        int randomPosition = int (double(std::rand()/2147483647.0) * gridSize);
    
        if(grid[randomPosition].insidePoly && !grid[randomPosition].nearEdge){
            return randomPosition;
        }
        
    }
    return -1;
    
}

void MainWindow::testDrawScrewPos(double xPos, double yPos){
    Transform3d transMatrix2 = transMatrixInverse;
    Transform3d transPos = Transform3d::Identity();
    transPos(0,3) = xPos;
    transPos(1,3) = yPos;
    transMatrix2 = transPos * transMatrix2;
    
    
    delete this->root_module;
	this->root_module = NULL;
	
    
    std::stringstream parseCommand;
    parseCommand << "module nutAssembly(){ cylinder(r= 6.5/2, h=100, $fn = 6, center=[0,0]); translate([0, 0, 1]) rotate(a=-180, v=[0,1,0]) cylinder(r= 3.8/2, h=100, $fn = 100, center=[0,0]); translate([0, 0, -5]) rotate(a=-180, v=[0,1,0]) cylinder(r= 3.5, h=100, $fn = 100, center=[0,0]);} ";
    
    parseCommand <<"multmatrix(m = [ [" << transMatrix2(0,0) << " , " << transMatrix2(0,1) << " , " << transMatrix2(0,2) << " , " << transMatrix2(0,3) << " ], ";
    parseCommand <<"[ " << transMatrix2(1,0) << " , " << transMatrix2(1,1) << " , " << transMatrix2(1,2) << " , " << transMatrix2(1,3) << " ], ";
    parseCommand <<"[ " << transMatrix2(2,0) << " , " << transMatrix2(2,1) << " , " << transMatrix2(2,2) << " , " << transMatrix2(2,3) << " ], ";
    parseCommand <<"[ " << transMatrix2(3,0) << " , " << transMatrix2(3,1) << " , " << transMatrix2(3,2) << " , " << transMatrix2(3,3) << " ] ] ) ";
    parseCommand << "nutAssembly(); ";
    
    
    setCurrentOutput();
    PRINT(parseCommand.str());
    clearCurrentOutput();
    
    const std::string tmp = parseCommand.str();
    const char* cstr = tmp.c_str();
    
	this->root_module = parse(cstr,"",false);
    instantiateRoot();
    
    
    PRINT(this->tree.getString(*this->root_node));
    
    csgRender();
    
}

void MainWindow::testDrawScrewPosAssembly(){
    
    Transform3d transMatrix1 = transMatrixInverse;
    Transform3d transPos1 = Transform3d::Identity();
    transPos1(0,3) = screw1X;
    transPos1(1,3) = screw1Y;
    transMatrix1 = transPos1 * transMatrix1;
    
    Transform3d transMatrix2 = transMatrixInverse;
    Transform3d transPos2 = Transform3d::Identity();
    transPos2(0,3) = screw2X;
    transPos2(1,3) = screw2Y;
    transMatrix2 = transPos2 * transMatrix2;
    
    Transform3d transMatrix3 = transMatrixInverse;
    Transform3d transPos3 = Transform3d::Identity();
    transPos3(0,3) = screw3X;
    transPos3(1,3) = screw3Y;
    transMatrix3 = transPos3 * transMatrix3;
    
    
    delete this->root_module;
	this->root_module = NULL;
	
    
    std::stringstream parseCommand;
    parseCommand << "module nutAssembly(){ cylinder(r= 6.5/2, h=100, $fn = 6, center=[0,0]); translate([0, 0, 1]) rotate(a=-180, v=[0,1,0]) cylinder(r= 3.8/2, h=100, $fn = 100, center=[0,0]); translate([0, 0, -5]) rotate(a=-180, v=[0,1,0]) cylinder(r= 3.5, h=100, $fn = 100, center=[0,0]);} ";
    
    parseCommand << "difference(){ {import(\"" ;
    parseCommand << enclosureFileName.toStdString();
    parseCommand << "\", convexity=2);} { ";
    
    parseCommand << "rotate(a=" << -1*zRot <<", v=[0,0,1]) "; //invert insertion direction rotation
    parseCommand << "rotate(a=" << -1*yRot <<", v=[0,1,0]) ";
    parseCommand << "rotate(a=" << -1*xRot <<", v=[1,0,0]) ";
    
    
    parseCommand << "rotate(a=90, v=[0,1,0]) ";     //invert -90Y rot for binvox
    parseCommand << "translate([" << binvoxTransX << ", "<< binvoxTransY << ", "<< binvoxTransZ << "]) ";
    parseCommand << "scale([" << binvoxScale << ", "<< binvoxScale << ", "<< binvoxScale << "]) ";
    parseCommand << "rotate(a=90, v=[1,0,0]) rotate(a=90, v=[0,1,0] ) import(\"trianglesExp.stl\", convexity=2);} "; //rotate to get back to correct position (I think due to a x,y,z read order error when importing binvox volume corrdinates to marching cubes app)

    parseCommand <<"multmatrix(m = [ [" << transMatrix1(0,0) << " , " << transMatrix1(0,1) << " , " << transMatrix1(0,2) << " , " << transMatrix1(0,3) << " ], ";
    parseCommand <<"[ " << transMatrix1(1,0) << " , " << transMatrix1(1,1) << " , " << transMatrix1(1,2) << " , " << transMatrix1(1,3) << " ], ";
    parseCommand <<"[ " << transMatrix1(2,0) << " , " << transMatrix1(2,1) << " , " << transMatrix1(2,2) << " , " << transMatrix1(2,3) << " ], ";
    parseCommand <<"[ " << transMatrix1(3,0) << " , " << transMatrix1(3,1) << " , " << transMatrix1(3,2) << " , " << transMatrix1(3,3) << " ] ] ) ";
    parseCommand << "nutAssembly();";
    
    parseCommand <<"multmatrix(m = [ [" << transMatrix2(0,0) << " , " << transMatrix2(0,1) << " , " << transMatrix2(0,2) << " , " << transMatrix2(0,3) << " ], ";
    parseCommand <<"[ " << transMatrix2(1,0) << " , " << transMatrix2(1,1) << " , " << transMatrix2(1,2) << " , " << transMatrix2(1,3) << " ], ";
    parseCommand <<"[ " << transMatrix2(2,0) << " , " << transMatrix2(2,1) << " , " << transMatrix2(2,2) << " , " << transMatrix2(2,3) << " ], ";
    parseCommand <<"[ " << transMatrix2(3,0) << " , " << transMatrix2(3,1) << " , " << transMatrix2(3,2) << " , " << transMatrix2(3,3) << " ] ] ) ";
    parseCommand << "nutAssembly();";
    
    
    parseCommand <<"multmatrix(m = [ [" << transMatrix3(0,0) << " , " << transMatrix3(0,1) << " , " << transMatrix3(0,2) << " , " << transMatrix3(0,3) << " ], ";
    parseCommand <<"[ " << transMatrix3(1,0) << " , " << transMatrix3(1,1) << " , " << transMatrix3(1,2) << " , " << transMatrix3(1,3) << " ], ";
    parseCommand <<"[ " << transMatrix3(2,0) << " , " << transMatrix3(2,1) << " , " << transMatrix3(2,2) << " , " << transMatrix3(2,3) << " ], ";
    parseCommand <<"[ " << transMatrix3(3,0) << " , " << transMatrix3(3,1) << " , " << transMatrix3(3,2) << " , " << transMatrix3(3,3) << " ] ] ) ";
    parseCommand << "nutAssembly();} ";
    
    
    setCurrentOutput();
    PRINT(parseCommand.str());
    clearCurrentOutput();
    
    const std::string tmp = parseCommand.str();
    const char* cstr = tmp.c_str();
    
	this->root_module = parse(cstr,"",false);
    instantiateRoot();
    
    
    PRINT(this->tree.getString(*this->root_node));
    
    csgRender();
    
}
