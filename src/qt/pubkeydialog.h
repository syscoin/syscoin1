#ifndef PUBKEYDIALOG_H
#define PUBKEYDIALOG_H

#include <QDialog>

namespace Ui {
    class PubKeyDialog;
}

QT_BEGIN_NAMESPACE

QT_END_NAMESPACE

/** Dialog for editing an address and associated information.
 */
class PubKeyDialog : public QDialog
{
    Q_OBJECT

public:


    explicit PubKeyDialog(QWidget *parent = 0);
    ~PubKeyDialog();

public slots:
    void accept();
private:
    Ui::PubKeyDialog *ui;

};

#endif // PUBKEYDIALOG_H
