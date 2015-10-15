#include <boost/assign/list_of.hpp>
#include <boost/foreach.hpp>

#include "offerlistpage.h"
#include "ui_offerlistpage.h"
#include "offertablemodel.h"
#include "optionsmodel.h"
#include "offerview.h"
#include "walletmodel.h"
#include "bitcoingui.h"
#include "bitcoinrpc.h"
#include "resellofferdialog.h"
#include "csvmodelwriter.h"
#include "guiutil.h"
#include "ui_interface.h"
#include <QSortFilterProxyModel>
#include <QClipboard>
#include <QMessageBox>
#include <QKeyEvent>
#include <QMenu>
#include "main.h"
using namespace std;
using namespace json_spirit;

extern const CRPCTable tableRPC;
extern string JSONRPCReply(const Value& result, const Value& error, const Value& id);
int GetOfferDisplayExpirationDepth();
OfferListPage::OfferListPage(OfferView *parent) :
    QDialog(0),
    ui(new Ui::OfferListPage),
    model(0),
    optionsModel(0),
	offerView(parent)
{
    ui->setupUi(this);

#ifdef Q_OS_MAC // Icons on push buttons are very uncommon on Mac
    ui->copyOffer->setIcon(QIcon());
    ui->exportButton->setIcon(QIcon());
	ui->resellButton->setIcon(QIcon());
	ui->purchaseButton->setIcon(QIcon());
#endif

    ui->labelExplanation->setText(tr("Search for Syscoin Offers"));
	
    // Context menu actions
    QAction *copyOfferAction = new QAction(ui->copyOffer->text(), this);
    QAction *copyOfferValueAction = new QAction(tr("&Copy Value"), this);
	QAction *resellAction = new QAction(tr("&Resell Offer"), this);
	QAction *purchaseAction = new QAction(tr("&Purchase Offer"), this);

    // Build context menu
    contextMenu = new QMenu();
    contextMenu->addAction(copyOfferAction);
    contextMenu->addAction(copyOfferValueAction);
	contextMenu->addSeparator();
	contextMenu->addAction(resellAction);
	contextMenu->addAction(purchaseAction);
    // Connect signals for context menu actions
    connect(copyOfferAction, SIGNAL(triggered()), this, SLOT(on_copyOffer_clicked()));
    connect(copyOfferValueAction, SIGNAL(triggered()), this, SLOT(onCopyOfferValueAction()));
	connect(resellAction, SIGNAL(triggered()), this, SLOT(on_resellButton_clicked()));
	connect(purchaseAction, SIGNAL(triggered()), this, SLOT(on_purchaseButton_clicked()));
    connect(ui->tableView, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(contextualMenu(QPoint)));


	ui->lineEditOfferSearch->setPlaceholderText(tr("Enter search term, regex accepted (ie: ^name returns all Offerificates starting with 'name')"));
}

OfferListPage::~OfferListPage()
{
    delete ui;
}
void OfferListPage::showEvent ( QShowEvent * event )
{
    if(!walletModel) return;
    /*if(walletModel->getEncryptionStatus() == WalletModel::Locked)
	{
        ui->labelExplanation->setText(tr("<font color='red'>WARNING: Your wallet is currently locked. For security purposes you'll need to enter your passphrase in order to search Syscoin Offers.</font> <a href=\"http://lockedwallet.syscoin.org\">more info</a>"));
		ui->labelExplanation->setTextFormat(Qt::RichText);
		ui->labelExplanation->setTextInteractionFlags(Qt::TextBrowserInteraction);
		ui->labelExplanation->setOpenExternalLinks(true);
    }*/
}
void OfferListPage::setModel(WalletModel* walletModel, OfferTableModel *model)
{
    this->model = model;
	this->walletModel = walletModel;
    if(!model) return;

    proxyModel = new QSortFilterProxyModel(this);
    proxyModel->setSourceModel(model);
    proxyModel->setDynamicSortFilter(true);
    proxyModel->setSortCaseSensitivity(Qt::CaseInsensitive);
    proxyModel->setFilterCaseSensitivity(Qt::CaseInsensitive);
    proxyModel->setFilterRole(OfferTableModel::TypeRole);
    proxyModel->setFilterFixedString(OfferTableModel::Offer);
    ui->tableView->setModel(proxyModel);
    ui->tableView->sortByColumn(0, Qt::AscendingOrder);

    // Set column widths
#if QT_VERSION < 0x050000
    ui->tableView->horizontalHeader()->setResizeMode(OfferTableModel::Name, QHeaderView::ResizeToContents);
    ui->tableView->horizontalHeader()->setResizeMode(OfferTableModel::Title, QHeaderView::Stretch);
	ui->tableView->horizontalHeader()->setResizeMode(OfferTableModel::Description, QHeaderView::Stretch);
    ui->tableView->horizontalHeader()->setResizeMode(OfferTableModel::Category, QHeaderView::ResizeToContents);
    ui->tableView->horizontalHeader()->setResizeMode(OfferTableModel::Price, QHeaderView::ResizeToContents);
	ui->tableView->horizontalHeader()->setResizeMode(OfferTableModel::Currency, QHeaderView::ResizeToContents);
	ui->tableView->horizontalHeader()->setResizeMode(OfferTableModel::Qty, QHeaderView::ResizeToContents);
	ui->tableView->horizontalHeader()->setResizeMode(OfferTableModel::ExclusiveResell, QHeaderView::ResizeToContents);
	ui->tableView->horizontalHeader()->setResizeMode(OfferTableModel::Expired, QHeaderView::ResizeToContents);
#else
    ui->tableView->horizontalHeader()->setSectionResizeMode(OfferTableModel::Name, QHeaderView::ResizeToContents);
    ui->tableView->horizontalHeader()->setSectionResizeMode(OfferTableModel::Title, QHeaderView::Stretch);
	ui->tableView->horizontalHeader()->setSectionResizeMode(OfferTableModel::Description, QHeaderView::Stretch);
    ui->tableView->horizontalHeader()->setSectionResizeMode(OfferTableModel::Category, QHeaderView::ResizeToContents);
    ui->tableView->horizontalHeader()->setSectionResizeMode(OfferTableModel::Price, QHeaderView::ResizeToContents);
	ui->tableView->horizontalHeader()->setSectionResizeMode(OfferTableModel::Currency, QHeaderView::ResizeToContents);
	ui->tableView->horizontalHeader()->setSectionResizeMode(OfferTableModel::Qty, QHeaderView::ResizeToContents);
	ui->tableView->horizontalHeader()->setSectionResizeMode(OfferTableModel::ExclusiveResell, QHeaderView::ResizeToContents);
    ui->tableView->horizontalHeader()->setSectionResizeMode(OfferTableModel::Expired, QHeaderView::ResizeToContents);
#endif


    connect(ui->tableView->selectionModel(), SIGNAL(selectionChanged(QItemSelection,QItemSelection)),
            this, SLOT(selectionChanged()));


    // Select row for newly created offer
    connect(model, SIGNAL(rowsInserted(QModelIndex,int,int)), this, SLOT(selectNewOffer(QModelIndex,int,int)));

    selectionChanged();

}

void OfferListPage::setOptionsModel(OptionsModel *optionsModel)
{
    this->optionsModel = optionsModel;
}

void OfferListPage::on_copyOffer_clicked()
{
   
    GUIUtil::copyEntryData(ui->tableView, OfferTableModel::Name);
}
void OfferListPage::on_resellButton_clicked()
{
 	if(!model)	
		return;
	if(!ui->tableView->selectionModel())
        return;
    QModelIndexList selection = ui->tableView->selectionModel()->selectedRows();
    if(selection.isEmpty())
    {
        return;
    }
    ResellOfferDialog dlg((QModelIndex*)&selection.at(0));   
    dlg.exec();
}
void OfferListPage::on_purchaseButton_clicked()
{
 	if(!model)	
		return;
	if(!ui->tableView->selectionModel())
        return;
    QModelIndexList selection = ui->tableView->selectionModel()->selectedRows();
    if(selection.isEmpty())
    {
        return;
    }
	QString offerGUID = selection.at(0).data(OfferTableModel::NameRole).toString();
	QString URI = QString("syscoin:///") + offerGUID + QString("?qty=1");
	offerView->handleURI(URI);
}

void OfferListPage::onCopyOfferValueAction()
{
    GUIUtil::copyEntryData(ui->tableView, OfferTableModel::Title);
}


void OfferListPage::selectionChanged()
{
    // Set button states based on selected tab and selection
    QTableView *table = ui->tableView;
    if(!table->selectionModel())
        return;

    if(table->selectionModel()->hasSelection())
    {
        ui->copyOffer->setEnabled(true);
		ui->resellButton->setEnabled(true);
		ui->purchaseButton->setEnabled(true);
		
    }
    else
    {
        ui->copyOffer->setEnabled(false);
		ui->resellButton->setEnabled(false);
		ui->purchaseButton->setEnabled(false);
    }
}
void OfferListPage::keyPressEvent(QKeyEvent * event)
{
  if( event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter )
  {
	on_searchOffer_clicked();
    event->accept();
  }
  else
    QDialog::keyPressEvent( event );
}
void OfferListPage::on_exportButton_clicked()
{
    // CSV is currently the only supported format
    QString filename = GUIUtil::getSaveFileName(
            this,
            tr("Export Offer Data"), QString(),
            tr("Comma separated file (*.csv)"));

    if (filename.isNull()) return;

    CSVModelWriter writer(filename);

    // name, column, role
    writer.setModel(proxyModel);
    writer.addColumn("Offer", OfferTableModel::Name, Qt::EditRole);
    writer.addColumn("Title", OfferTableModel::Title, Qt::EditRole);
	writer.addColumn("Description", OfferTableModel::Title, Qt::EditRole);
	writer.addColumn("Category", OfferTableModel::Category, Qt::EditRole);
	writer.addColumn("Price", OfferTableModel::Price, Qt::EditRole);
	writer.addColumn("Currency", OfferTableModel::Currency, Qt::EditRole);
	writer.addColumn("Qty", OfferTableModel::Qty, Qt::EditRole);
	writer.addColumn("Exclusive Resell", OfferTableModel::ExclusiveResell, Qt::EditRole);
	writer.addColumn("Expired", OfferTableModel::Expired, Qt::EditRole);
    if(!writer.write())
    {
        QMessageBox::critical(this, tr("Error exporting"), tr("Could not write to file %1.").arg(filename),
                              QMessageBox::Abort, QMessageBox::Abort);
    }
}



void OfferListPage::contextualMenu(const QPoint &point)
{
    QModelIndex index = ui->tableView->indexAt(point);
    if(index.isValid()) {
        contextMenu->exec(QCursor::pos());
    }
}

void OfferListPage::selectNewOffer(const QModelIndex &parent, int begin, int /*end*/)
{
    QModelIndex idx = proxyModel->mapFromSource(model->index(begin, OfferTableModel::Name, parent));
    if(idx.isValid() && (idx.data(Qt::EditRole).toString() == newOfferToSelect))
    {
        // Select row of newly created offer, once
        ui->tableView->setFocus();
        ui->tableView->selectRow(idx.row());
        newOfferToSelect.clear();
    }
}

void OfferListPage::on_searchOffer_clicked()
{
    if(!walletModel) return;
    if(ui->lineEditOfferSearch->text().trimmed().isEmpty())
    {
        QMessageBox::warning(this, tr("Error Searching Offers"),
            tr("Please enter search term"),
            QMessageBox::Ok, QMessageBox::Ok);
        return;
    }
        Array params;
        Value valError;
        Object ret ;
        Value valResult;
        Array arr;
        Value valId;
        Value result ;
        string strReply;
        string strError;
        string strMethod = string("offerfilter");
		string name_str;
		string value_str;
		string desc_str;
		string category_str;
		string price_str;
		string currency_str;
		string qty_str;
		string expired_str;
		string exclusive_resell_str;
		int expired = 0;
        params.push_back(ui->lineEditOfferSearch->text().toStdString());
        params.push_back(GetOfferDisplayExpirationDepth());

        try {
            result = tableRPC.execute(strMethod, params);
        }
        catch (Object& objError)
        {
            strError = find_value(objError, "message").get_str();
            QMessageBox::critical(this, windowTitle(),
            tr("Error searching Offer: \"%1\"").arg(QString::fromStdString(strError)),
                QMessageBox::Ok, QMessageBox::Ok);
            return;
        }
        catch(std::exception& e)
        {
            QMessageBox::critical(this, windowTitle(),
                tr("General exception when searching offer"),
                QMessageBox::Ok, QMessageBox::Ok);
            return;
        }
		if (result.type() == array_type)
			{
				this->model->clear();
			
			  Array arr = result.get_array();
			  BOOST_FOREACH(Value& input, arr)
				{
				if (input.type() != obj_type)
					continue;
				Object& o = input.get_obj();
				name_str = "";
				value_str = "";
				desc_str = "";
				exclusive_resell_str = "";
				expired = 0;


				const Value& name_value = find_value(o, "offer");
				if (name_value.type() == str_type)
					name_str = name_value.get_str();
				const Value& value_value = find_value(o, "title");
				if (value_value.type() == str_type)
					value_str = value_value.get_str();
				const Value& desc_value = find_value(o, "description");
				if (desc_value.type() == str_type)
					desc_str = desc_value.get_str();
				const Value& category_value = find_value(o, "category");
				if (category_value.type() == str_type)
					category_str = category_value.get_str();
				const Value& price_value = find_value(o, "price");
				if (price_value.type() == str_type)
					price_str = price_value.get_str();
				const Value& currency_value = find_value(o, "currency");
				if (currency_value.type() == str_type)
					currency_str = currency_value.get_str();
				const Value& qty_value = find_value(o, "quantity");
				if (qty_value.type() == str_type)
					qty_str = qty_value.get_str();
				const Value& exclusive_resell_value = find_value(o, "exclusive_resell");
				if (exclusive_resell_value.type() == str_type)
					exclusive_resell_str = exclusive_resell_value.get_str();				
				const Value& expired_value = find_value(o, "expired");
				if (expired_value.type() == int_type)
					expired = expired_value.get_int();

				if(expired == 1)
				{
					expired_str = "Expired";
				}
				else
				{
					expired_str = "Valid";
				}

				
				
				model->addRow(OfferTableModel::Offer,
						QString::fromStdString(name_str),
						QString::fromStdString(value_str),
						QString::fromStdString(desc_str),
						QString::fromStdString(category_str),
						QString::fromStdString(price_str),
						QString::fromStdString(currency_str),
						QString::fromStdString(qty_str),
						QString::fromStdString(expired_str), QString::fromStdString(exclusive_resell_str));
					this->model->updateEntry(QString::fromStdString(name_str),
						QString::fromStdString(value_str),
						QString::fromStdString(desc_str),
						QString::fromStdString(category_str),
						QString::fromStdString(price_str),
						QString::fromStdString(currency_str),
						QString::fromStdString(qty_str),
						QString::fromStdString(expired_str),QString::fromStdString(exclusive_resell_str), AllOffer, CT_NEW);	
			  }

            
         }   
        else
        {
            QMessageBox::critical(this, windowTitle(),
                tr("Error: Invalid response from offerfilter command"),
                QMessageBox::Ok, QMessageBox::Ok);
            return;
        }

}

bool OfferListPage::handleURI(const QString& strURI)
{
 
    return false;
}
