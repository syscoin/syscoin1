/*
 * Qt4/Qt5 Syscoin GUI.
 *
 * Sidhujag
 * Syscoin Developer 2014
 */
#ifndef OFFERVIEW_H
#define OFFERVIEW_H

#include <QStackedWidget>

class BitcoinGUI;

class AcceptandPayOfferListPage;

class COffer;

QT_BEGIN_NAMESPACE
class QObject;
class QWidget;
class QLabel;
class QModelIndex;
class QTabWidget;
class QStackedWidget;
class QAction;
QT_END_NAMESPACE

/*
  OfferView class. This class represents the view to the syscoin offer marketplace
  
*/
class OfferView: public QObject
 {
     Q_OBJECT

public:
    explicit OfferView(QStackedWidget *parent, BitcoinGUI *_gui);
    ~OfferView();

    void setBitcoinGUI(BitcoinGUI *gui);
    /** Set the client model.
        The client model represents the part of the core that communicates with the P2P network, and is wallet-agnostic.
    */
	
    bool handleURI(const QString &uri);


private:
    BitcoinGUI *gui;

	QTabWidget *tabWidget;
	AcceptandPayOfferListPage *acceptandPayOfferListPage;
	

public:
    /** Switch to offer page */
    void gotoOfferListPage();

signals:
    /** Signal that we want to show the main window */
    void showNormalIfMinimized();
};

#endif // OFFERVIEW_H
