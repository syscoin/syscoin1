#include "init.h"
#include "util.h"
#include "offerpaydialog.h"
#include "ui_offerpaydialog.h"

#include "bitcoingui.h"
#include "bitcoinrpc.h"
#include <QProgressBar> 
#include <QTimer>
#include <QMessageBox>
using namespace std;
using namespace json_spirit;
extern const CRPCTable tableRPC;
OfferPayDialog::OfferPayDialog(QString title, QString quantity, QString price, QWidget *parent) :
    QDialog(parent), 
	ui(new Ui::OfferPayDialog)
{
    ui->setupUi(this);
	connect(ui->finishButton, SIGNAL(clicked()), this, SLOT(accept()));
	ui->payMessage->setText(tr("<p>You've purchased %1 of '%2' for %3 SYS!</p><p><FONT COLOR='green'><b>Your payment is complete!</b></FONT></p><p>The merchant has been sent your delivery information and your item should arrive shortly. The merchant may followup with further information.</p><br>").arg(quantity).arg(title).arg(price));			
	ui->purchaseHint->setText(tr("Please click Finish"));

}

OfferPayDialog::~OfferPayDialog()
{
    delete ui;
}


