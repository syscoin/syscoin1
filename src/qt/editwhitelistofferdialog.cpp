/*
 * Syscoin Developers 2015
 */
#include <boost/assign/list_of.hpp>
#include <boost/foreach.hpp>
#include "editwhitelistofferdialog.h"
#include "ui_editwhitelistofferdialog.h"
#include "offertablemodel.h"
#include "offerwhitelisttablemodel.h"
#include "walletmodel.h"
#include "bitcoingui.h"
#include "bitcoinrpc.h"
#include "newwhitelistdialog.h"
#include "csvmodelwriter.h"
#include "guiutil.h"
#include "script.h"
#include "ui_interface.h"
#include <QTableView>
#include <QSortFilterProxyModel>
#include <QClipboard>
#include <QMessageBox>
#include <QModelIndex>
#include <QMenu>
#include <QItemSelection>


using namespace std;
using namespace json_spirit;


extern const CRPCTable tableRPC;
extern int nBestHeight;
int64 GetOfferNetworkFee(opcodetype seed, unsigned int nHeight);
EditWhitelistOfferDialog::EditWhitelistOfferDialog(QModelIndex *idx, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::EditWhitelistOfferDialog),
    model(0),
	myIdx(idx)
{
    ui->setupUi(this);

#ifdef Q_OS_MAC // Icons on push buttons are very uncommon on Mac
    ui->newEntry->setIcon(QIcon());
	ui->refreshButton->setIcon(QIcon());
    ui->exportButton->setIcon(QIcon());
	ui->exclusiveButton->setIcon(QIcon());
	ui->removeButton->setIcon(QIcon());
	ui->removeAllButton->setIcon(QIcon());
#endif
	offerGUID = idx->data(OfferTableModel::NameRole).toString();
	exclusiveWhitelist = idx->data(OfferTableModel::ExclusiveWhitelistRole).toString();
	ui->buttonBox->setVisible(false);

    ui->labelExplanation->setText(tr("These are the whitelist entries for your offer. You may specify discount levels for each whitelist entry or control who may resell your offer if you are in Exclusive Resell Mode. If Exclusive Resell Mode is off anyone can resell your offers, although discounts will still be applied if they own a certificate that you've added to your whitelist."));
	
    // Context menu actions
    QAction *removeAction = new QAction(tr("&Remove"), this);
	QAction *copyAction = new QAction(tr("&Copy Certificate GUID"), this);
    // Build context menu
    contextMenu = new QMenu();
    contextMenu->addAction(copyAction);
	contextMenu->addSeparator();
	contextMenu->addAction(removeAction);

    connect(copyAction, SIGNAL(triggered()), this, SLOT(on_copy()));


    // Connect signals for context menu actions
    connect(removeAction, SIGNAL(triggered()), this, SLOT(on_removeButton_clicked()));

    connect(ui->tableView, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(contextualMenu(QPoint)));

    // Pass through accept action from button box
    connect(ui->buttonBox, SIGNAL(accepted()), this, SLOT(accept()));
	
}

EditWhitelistOfferDialog::~EditWhitelistOfferDialog()
{
    delete ui;
}

void EditWhitelistOfferDialog::setModel(WalletModel *walletModel, OfferWhitelistTableModel *model)
{
    this->model = model;
	this->walletModel = walletModel;
    if(!model) return;
    proxyModel = new QSortFilterProxyModel(this);
    proxyModel->setSourceModel(model);
    proxyModel->setDynamicSortFilter(true);
    proxyModel->setSortCaseSensitivity(Qt::CaseInsensitive);
    proxyModel->setFilterCaseSensitivity(Qt::CaseInsensitive);

  
		// Receive filter
		//proxyModel->setFilterRole(OfferWhitelistTableModel::CertRole);
		//proxyModel->setFilterFixedString(OfferWhitelistTableModel::Entry);
       
    
		ui->tableView->setModel(proxyModel);
		ui->tableView->sortByColumn(0, Qt::AscendingOrder);
        ui->tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
        ui->tableView->setSelectionMode(QAbstractItemView::SingleSelection);
    // Set column widths
#if QT_VERSION < 0x050000
    ui->tableView->horizontalHeader()->setResizeMode(OfferWhitelistTableModel::Cert, QHeaderView::ResizeToContents);
    ui->tableView->horizontalHeader()->setResizeMode(OfferWhitelistTableModel::Title, QHeaderView::Stretch);
	ui->tableView->horizontalHeader()->setResizeMode(OfferWhitelistTableModel::Mine, QHeaderView::ResizeToContents);
	ui->tableView->horizontalHeader()->setResizeMode(OfferWhitelistTableModel::Address, QHeaderView::ResizeToContents);
	ui->tableView->horizontalHeader()->setResizeMode(OfferWhitelistTableModel::Expires, QHeaderView::ResizeToContents);
	ui->tableView->horizontalHeader()->setResizeMode(OfferWhitelistTableModel::Discount, QHeaderView::ResizeToContents);
#else
    ui->tableView->horizontalHeader()->setSectionResizeMode(OfferWhitelistTableModel::Cert, QHeaderView::ResizeToContents);
    ui->tableView->horizontalHeader()->setSectionResizeMode(OfferWhitelistTableModel::Title, QHeaderView::Stretch);
	ui->tableView->horizontalHeader()->setSectionResizeMode(OfferWhitelistTableModel::Mine, QHeaderView::ResizeToContents);
	ui->tableView->horizontalHeader()->setSectionResizeMode(OfferWhitelistTableModel::Address, QHeaderView::ResizeToContents);
	ui->tableView->horizontalHeader()->setSectionResizeMode(OfferWhitelistTableModel::Expires, QHeaderView::ResizeToContents);
	ui->tableView->horizontalHeader()->setSectionResizeMode(OfferWhitelistTableModel::Discount, QHeaderView::ResizeToContents);
#endif

    connect(ui->tableView->selectionModel(), SIGNAL(selectionChanged(QItemSelection,QItemSelection)),
            this, SLOT(selectionChanged()));

    // Select row for newly created offer
    connect(model, SIGNAL(rowsInserted(QModelIndex,int,int)), this, SLOT(selectNewEntry(QModelIndex,int,int)));

    selectionChanged();
	model->clear();
	on_refreshButton_clicked();

}



void EditWhitelistOfferDialog::on_copy()
{
    GUIUtil::copyEntryData(ui->tableView, OfferWhitelistTableModel::Cert);
}
void EditWhitelistOfferDialog::on_exclusiveButton_clicked()
{
	QString tmpExclusiveWhitelist = exclusiveWhitelist;
	
	double updateFee = (double)GetOfferNetworkFee(OP_OFFER_UPDATE, nBestHeight)/(double)COIN;
	string updateFeeStr = strprintf("%.2f", updateFee);
    QMessageBox::StandardButton retval = QMessageBox::question(this, tr("Confirm whitelist mode change"),
             tr("Warning: Updating exclusive whitelist mode will cost ") + QString::fromStdString(updateFeeStr) + " SYS<br><br>" + tr("Do you want to continue?"),
             QMessageBox::Yes|QMessageBox::Cancel,
             QMessageBox::Cancel);
    if(retval == QMessageBox::Yes)
    {
		string strError;
		string strMethod = string("offerupdate");
		Array params;
		Value result;
		params.push_back(myIdx->data(OfferTableModel::NameRole).toString().toStdString());
		params.push_back(myIdx->data(OfferTableModel::CategoryRole).toString().toStdString());
		params.push_back(myIdx->data(OfferTableModel::TitleRole).toString().toStdString());
		params.push_back(myIdx->data(OfferTableModel::QtyRole).toString().toStdString());
		params.push_back(myIdx->data(OfferTableModel::PriceRole).toString().toStdString());
		params.push_back(myIdx->data(OfferTableModel::DescriptionRole).toString().toStdString());
		if(tmpExclusiveWhitelist == QString("true"))
			params.push_back("0");
		else 
			params.push_back("1");
		try {
			result = tableRPC.execute(strMethod, params);
			QMessageBox::information(this, windowTitle(),
			tr("Whitelist exclusive mode changed successfully!"),
				QMessageBox::Ok, QMessageBox::Ok);

			if(tmpExclusiveWhitelist == QString("true"))
				exclusiveWhitelist = QString("false");
			else
				exclusiveWhitelist = QString("true");

		}
		catch (Object& objError)
		{
			string strError = find_value(objError, "message").get_str();
			QMessageBox::critical(this, windowTitle(),
				tr("Could not change the whitelist mode: %1").arg(QString::fromStdString(strError)),
					QMessageBox::Ok, QMessageBox::Ok);

		}
		catch(std::exception& e)
		{
			QMessageBox::critical(this, windowTitle(),
				tr("There was an exception trying to change the whitelist mode: ") + QString::fromStdString(e.what()),
					QMessageBox::Ok, QMessageBox::Ok);
		}

	}
	if(exclusiveWhitelist == QString("true"))
	{
		ui->exclusiveButton->setText(tr("Exclusive Mode On"));
	}
	else
	{
		ui->exclusiveButton->setText(tr("Exclusive Mode Off"));
	}	
}

void EditWhitelistOfferDialog::on_removeButton_clicked()
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
	QString certGUID = selection.at(0).data(OfferWhitelistTableModel::Cert).toString();

	double updateFee = (double)GetOfferNetworkFee(OP_OFFER_UPDATE, nBestHeight)/(double)COIN;
	string updateFeeStr = strprintf("%.2f", updateFee);
    QMessageBox::StandardButton retval = QMessageBox::question(this, tr("Confirm whitelist entry remove"),
             tr("Warning: Removing an offer from the whitelist will cost ") + QString::fromStdString(updateFeeStr) + " SYS<br><br>" + tr("Do you want to continue?"),
             QMessageBox::Yes|QMessageBox::Cancel,
             QMessageBox::Cancel);
    if(retval == QMessageBox::Yes)
    {
		string strError;
		string strMethod = string("offerremovewhitelist");
		Array params;
		Value result;
		params.push_back(offerGUID.toStdString());
		params.push_back(certGUID.toStdString());

		try {
			result = tableRPC.execute(strMethod, params);
			QMessageBox::information(this, windowTitle(),
			tr("Entry removed successfully!"),
				QMessageBox::Ok, QMessageBox::Ok);
			model->updateEntry(certGUID, certGUID, certGUID, certGUID, certGUID, certGUID, CT_DELETED); 
		}
		catch (Object& objError)
		{
			string strError = find_value(objError, "message").get_str();
			QMessageBox::critical(this, windowTitle(),
				tr("Could not remove this entry: %1").arg(QString::fromStdString(strError)),
					QMessageBox::Ok, QMessageBox::Ok);

		}
		catch(std::exception& e)
		{
			QMessageBox::critical(this, windowTitle(),
				tr("There was an exception trying to remove this entry: ") + QString::fromStdString(e.what()),
					QMessageBox::Ok, QMessageBox::Ok);
		}

	}
}
void EditWhitelistOfferDialog::on_removeAllButton_clicked()
{
	if(!model)
		return;
	double updateFee = (double)GetOfferNetworkFee(OP_OFFER_UPDATE, nBestHeight)/(double)COIN;
	string updateFeeStr = strprintf("%.2f", updateFee);
    QMessageBox::StandardButton retval = QMessageBox::question(this, tr("Confirm whitelist clear"),
             tr("Warning: Clearing an offer whitelist will cost ") + QString::fromStdString(updateFeeStr) + " SYS<br><br>" + tr("Do you want to continue?"),
             QMessageBox::Yes|QMessageBox::Cancel,
             QMessageBox::Cancel);
    if(retval == QMessageBox::Yes)
    {
		string strError;
		string strMethod = string("offerclearwhitelist");
		Array params;
		Value result;
		params.push_back(offerGUID.toStdString());
		try {
			result = tableRPC.execute(strMethod, params);
			QMessageBox::information(this, windowTitle(),
			tr("Whitelist cleared successfully!"),
				QMessageBox::Ok, QMessageBox::Ok);
			model->clear();
		}
		catch (Object& objError)
		{
			string strError = find_value(objError, "message").get_str();
			QMessageBox::critical(this, windowTitle(),
				tr("Could not clear the whitelist: %1").arg(QString::fromStdString(strError)),
					QMessageBox::Ok, QMessageBox::Ok);

		}
		catch(std::exception& e)
		{
			QMessageBox::critical(this, windowTitle(),
				tr("There was an exception trying to clear the whitelist: ") + QString::fromStdString(e.what()),
					QMessageBox::Ok, QMessageBox::Ok);
		}

	}
}
void EditWhitelistOfferDialog::on_refreshButton_clicked()
{
	if(!model) return;
	string strError;
	string strMethod = string("offerwhitelist");
	Array params;
	Value result;
	params.push_back(offerGUID.toStdString());
	try {
		result = tableRPC.execute(strMethod, params);
		if (result.type() == array_type)
		{
			this->model->clear();
			string cert_str = "";
			string title_str = "";
			string mine_str = "";
			string cert_address_str = "";
			string cert_expiresin_str = "";
			string offer_discount_percentage_str = "";
			int cert_expiresin = 0;
			Array arr = result.get_array();
			BOOST_FOREACH(Value& input, arr)
			{
				if (input.type() != obj_type)
					continue;
				Object& o = input.get_obj();
				const Value& cert_value = find_value(o, "cert_guid");
				if (cert_value.type() == str_type)
					cert_str = cert_value.get_str();
				const Value& title_value = find_value(o, "cert_title");
				if (title_value.type() == str_type)
					title_str = title_value.get_str();
				const Value& mine_value = find_value(o, "cert_is_mine");
				if (mine_value.type() == str_type)
					mine_str = mine_value.get_str();
				const Value& cert_address_value = find_value(o, "cert_address");
				if (cert_address_value.type() == str_type)
					cert_address_str = cert_address_value.get_str();
				const Value& cert_expiresin_value = find_value(o, "cert_expiresin");
				if (cert_expiresin_value.type() == int_type)
					cert_expiresin = cert_expiresin_value.get_int();
				const Value& offer_discount_percentage_value = find_value(o, "offer_discount_percentage");
				if (offer_discount_percentage_value.type() == str_type)
					offer_discount_percentage_str = offer_discount_percentage_value.get_str();
				cert_expiresin_str = strprintf("%d Blocks", cert_expiresin);
				model->addRow(QString::fromStdString(cert_str), QString::fromStdString(title_str), QString::fromStdString(mine_str), QString::fromStdString(cert_address_str), QString::fromStdString(cert_expiresin_str), QString::fromStdString(offer_discount_percentage_str));
				model->updateEntry(QString::fromStdString(cert_str), QString::fromStdString(title_str), QString::fromStdString(mine_str), QString::fromStdString(cert_address_str), QString::fromStdString(cert_expiresin_str), QString::fromStdString(offer_discount_percentage_str), CT_NEW); 
			}
		}
	}
	catch (Object& objError)
	{
		string strError = find_value(objError, "message").get_str();
		QMessageBox::critical(this, windowTitle(),
			tr("Could not refresh the whitelist: %1").arg(QString::fromStdString(strError)),
				QMessageBox::Ok, QMessageBox::Ok);

	}
	catch(std::exception& e)
	{
		QMessageBox::critical(this, windowTitle(),
			tr("There was an exception trying to refresh the whitelist: ") + QString::fromStdString(e.what()),
				QMessageBox::Ok, QMessageBox::Ok);
	}

	if(exclusiveWhitelist == QString("true"))
	{
		ui->exclusiveButton->setText(tr("Exclusive Mode On"));
	}
	else
	{
		ui->exclusiveButton->setText(tr("Exclusive Mode Off"));
	}
	if(model->rowCount(*myIdx) > 0)
	{
		ui->removeAllButton->setEnabled(true);
	}
	else
	{
		ui->removeAllButton->setEnabled(false);
	}

}
void EditWhitelistOfferDialog::on_newEntry_clicked()
{
    if(!model)
        return;
    NewWhitelistDialog dlg(myIdx);   
	dlg.setModel(walletModel, model);
    dlg.exec();
}
void EditWhitelistOfferDialog::selectionChanged()
{
    // Set button states based on selected tab and selection
    QTableView *table = ui->tableView;
    if(!table->selectionModel())
        return;

    if(table->selectionModel()->hasSelection())
    {
        ui->removeButton->setEnabled(true);
    }
    else
    {
        ui->removeButton->setEnabled(false);
    }
}


void EditWhitelistOfferDialog::on_exportButton_clicked()
{
    // CSV is currently the only supported format
    QString filename = GUIUtil::getSaveFileName(
            this,
            tr("Export Whitelist Data"), QString(),
            tr("Comma separated file (*.csv)"));

    if (filename.isNull()) return;

    CSVModelWriter writer(filename);
    // name, column, role
    writer.setModel(proxyModel);
    writer.addColumn("Cert", OfferWhitelistTableModel::Cert, Qt::EditRole);
	writer.addColumn("Title", OfferWhitelistTableModel::Title, Qt::EditRole);
	writer.addColumn("Mine", OfferWhitelistTableModel::Mine, Qt::EditRole);
	writer.addColumn("Address", OfferWhitelistTableModel::Address, Qt::EditRole);
	writer.addColumn("Expires", OfferWhitelistTableModel::Expires, Qt::EditRole);
	writer.addColumn("Discount", OfferWhitelistTableModel::Discount, Qt::EditRole);
	
    if(!writer.write())
    {
        QMessageBox::critical(this, tr("Error exporting"), tr("Could not write to file %1.").arg(filename),
                              QMessageBox::Abort, QMessageBox::Abort);
    }
}



void EditWhitelistOfferDialog::contextualMenu(const QPoint &point)
{
    QModelIndex index = ui->tableView->indexAt(point);
    if(index.isValid()) {
        contextMenu->exec(QCursor::pos());
    }
}

void EditWhitelistOfferDialog::selectNewEntry(const QModelIndex &parent, int begin, int /*end*/)
{
    QModelIndex idx = proxyModel->mapFromSource(model->index(begin, OfferWhitelistTableModel::Cert, parent));
    if(idx.isValid() && (idx.data(Qt::EditRole).toString() == newEntryToSelect))
    {
        // Select row of newly created offer, once
        ui->tableView->setFocus();
        ui->tableView->selectRow(idx.row());
        newEntryToSelect.clear();
    }
}
