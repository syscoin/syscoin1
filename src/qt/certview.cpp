/*
 * Syscoin Developers 2015
 */
#include "certview.h"
#include "bitcoingui.h"


#include "clientmodel.h"
#include "walletmodel.h"
#include "optionsmodel.h"
#include "mycertlistpage.h"
#include "certlistpage.h"
#include "certtablemodel.h"
#include "ui_interface.h"

#include <QAction>
#if QT_VERSION < 0x050000
#include <QDesktopServices>
#else
#include <QStandardPaths>
#endif
#include <QPushButton>

CertView::CertView(QStackedWidget *parent, BitcoinGUI *_gui):
    gui(_gui),
    clientModel(0),
    walletModel(0)
{
	tabWidget = new QTabWidget();
    certListPage = new CertListPage();
    myCertListPage = new MyCertListPage();
	
	tabWidget->addTab(myCertListPage, tr("&My Certificates"));
	tabWidget->addTab(certListPage, tr("&Search"));
	tabWidget->setTabIcon(0, QIcon(":/icons/cert"));
	tabWidget->setTabIcon(1, QIcon(":/icons/search"));
	parent->addWidget(tabWidget);

}

CertView::~CertView()
{
}

void CertView::setBitcoinGUI(BitcoinGUI *gui)
{
    this->gui = gui;
}

void CertView::setClientModel(ClientModel *clientModel)
{
    this->clientModel = clientModel;
    if (clientModel)
    {
       
        certListPage->setOptionsModel(clientModel->getOptionsModel());
		myCertListPage->setOptionsModel(clientModel,clientModel->getOptionsModel());

    }
}

void CertView::setWalletModel(WalletModel *walletModel)
{

    this->walletModel = walletModel;
    if (walletModel)
    {

        certListPage->setModel(walletModel, walletModel->getCertTableModelAll());
		myCertListPage->setModel(walletModel, walletModel->getCertTableModelMine());

    }
}


void CertView::gotoCertListPage()
{
	tabWidget->setCurrentWidget(certListPage);
}


bool CertView::handleURI(const QString& strURI)
{
 // URI has to be valid
    if (certListPage->handleURI(strURI))
    {
        gotoCertListPage();
        emit showNormalIfMinimized();
        return true;
    }

    // URI has to be valid
    else if (myCertListPage->handleURI(strURI))
    {
        tabWidget->setCurrentWidget(myCertListPage);
        emit showNormalIfMinimized();
        return true;
    }
    
    return false;
}
