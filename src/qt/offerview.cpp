/*
 * Qt4/Qt5 Syscoin GUI.
 *
 * Sidhujag
 * Syscoin Developer 2014
 */
#include "offerview.h"
#include "bitcoingui.h"


#include "wallet.h"

#include "acceptandpayofferlistpage.h"

#include "ui_interface.h"

#include <QAction>
#if QT_VERSION < 0x050000
#include <QDesktopServices>
#else
#include <QStandardPaths>
#endif
#include <QFileDialog>
#include <QPushButton>

OfferView::OfferView(QStackedWidget *parent, BitcoinGUI *_gui):
    gui(_gui)
{
	tabWidget = new QTabWidget();
	acceptandPayOfferListPage = new AcceptandPayOfferListPage();
	tabWidget->addTab(acceptandPayOfferListPage, tr("Accept and &Pay for Offer"));
	parent->addWidget(tabWidget);
}

OfferView::~OfferView()
{
}

void OfferView::setBitcoinGUI(BitcoinGUI *gui)
{
    this->gui = gui;
}




void OfferView::gotoOfferListPage()
{
	tabWidget->setCurrentWidget(acceptandPayOfferListPage);
}


bool OfferView::handleURI(const QString& strURI)
{
	COffer myOffer;
    if (acceptandPayOfferListPage->handleURI(strURI, myOffer))
    {
        tabWidget->setCurrentWidget(acceptandPayOfferListPage);
		acceptandPayOfferListPage->setValue(myOffer);
        return true;
    }
 
   return false;
}
