#ifndef OFFERACCEPTDIALOG_H
#define OFFERACCEPTDIALOG_H

#include <QDialog>

namespace Ui {
    class OfferAcceptDialog;
}

class COffer;
/** Dialog for editing an address and associated information.
 */
class OfferAcceptDialog : public QDialog
{
    Q_OBJECT

public:
    explicit OfferAcceptDialog(COffer& Offer, QString notes, QWidget *parent = 0);
    ~OfferAcceptDialog();

    bool getPaymentStatus();

private:
    Ui::OfferAcceptDialog *ui;
	COffer& offer;
	QString notes;
	QString offerAcceptGUID;
	QString offerAcceptTXID;
	bool offerPaid; 

private slots:
    void accept();
};

#endif // OFFERACCEPTDIALOG_H
