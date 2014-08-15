/*
 * Qt4/Qt5 Syscoin GUI.
 *
 * Sidhujag
 * Syscoin Developer 2014
 */
#include "offerview.h"
#include "bitcoingui.h"


#include "clientmodel.h"
#include "walletmodel.h"
#include "optionsmodel.h"
#include "myofferlistpage.h"
#include "allofferlistpage.h"
#include "acceptandpayofferlistpage.h"
#include "acceptedofferlistpage.h"
#include "offertablemodel.h"
#include "ui_interface.h"

#include <QAction>
#if QT_VERSION < 0x050000
#include <QDesktopServices>
#else
#include <QStandardPaths>
#endif
#include <QPushButton>

OfferView::OfferView(QStackedWidget *parent, BitcoinGUI *_gui):
    gui(_gui),
    clientModel(0),
    walletModel(0)
{
	tabWidget = new QTabWidget();
    allOfferListPage = new AllOfferListPage();
    myOfferListPage = new MyOfferListPage();
	acceptandPayOfferListPage = new AcceptandPayOfferListPage();
	acceptedOfferListPage = new AcceptedOfferListPage();
	tabWidget->addTab(allOfferListPage, tr("All &Offers"));
	tabWidget->addTab(myOfferListPage, tr("&My Offers"));
	tabWidget->addTab(acceptandPayOfferListPage, tr("Accept and &Pay for Offer"));
	tabWidget->addTab(acceptedOfferListPage, tr("&Accepted Offers"));
	parent->addWidget(tabWidget);

}

OfferView::~OfferView()
{
}

void OfferView::setBitcoinGUI(BitcoinGUI *gui)
{
    this->gui = gui;
}

void OfferView::setClientModel(ClientModel *clientModel)
{
    this->clientModel = clientModel;
    if (clientModel)
    {
       
        allOfferListPage->setOptionsModel(clientModel->getOptionsModel());
		myOfferListPage->setOptionsModel(clientModel->getOptionsModel());
		acceptandPayOfferListPage->setOptionsModel(clientModel->getOptionsModel());
		acceptedOfferListPage->setOptionsModel(clientModel->getOptionsModel());
 
    }
}

void OfferView::setWalletModel(WalletModel *walletModel)
{
    this->walletModel = walletModel;
    if (walletModel)
    {
        allOfferListPage->setModel(walletModel->getOfferTableModelAll());
		myOfferListPage->setModel(walletModel->getOfferTableModelMine());
		acceptandPayOfferListPage->setModel(NULL);
		acceptedOfferListPage->setModel(walletModel->getOfferTableModelMine());
    }
}


void OfferView::gotoOfferListPage()
{
	tabWidget->setCurrentWidget(allOfferListPage);
}


bool OfferView::handleURI(const QString& strURI)
{
 // URI has to be valid
    if (allOfferListPage->handleURI(strURI))
    {
        gotoOfferListPage();
        emit showNormalIfMinimized();
        return true;
    }

    // URI has to be valid
    else if (myOfferListPage->handleURI(strURI))
    {
        tabWidget->setCurrentWidget(myOfferListPage);
        emit showNormalIfMinimized();
        return true;
    }
    else if (acceptandPayOfferListPage->handleURI(strURI))
    {
        tabWidget->setCurrentWidget(acceptandPayOfferListPage);
        emit showNormalIfMinimized();
        return true;
    }
    else if (acceptedOfferListPage->handleURI(strURI))
    {
        tabWidget->setCurrentWidget(acceptedOfferListPage);
        emit showNormalIfMinimized();
        return true;
    }
    else
        return false;
}
