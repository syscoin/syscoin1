#ifndef EDITCERTDIALOG_H
#define EDITCERTDIALOG_H

#include <QDialog>

namespace Ui {
    class EditCertDialog;
}
class CertTableModel;

QT_BEGIN_NAMESPACE
class QDataWidgetMapper;
QT_END_NAMESPACE

/** Dialog for editing an address and associated information.
 */
class EditCertDialog : public QDialog
{
    Q_OBJECT

public:
    enum Mode {
        NewCert,
        EditCert
    };

    explicit EditCertDialog(Mode mode, QWidget *parent = 0);
    ~EditCertDialog();

    void setModel(CertTableModel *model);
    void loadRow(int row);

    QString getCert() const;
    void setCert(const QString &cert);

public slots:
    void accept();

private:
    bool saveCurrentRow();

    Ui::EditCertDialog *ui;
    QDataWidgetMapper *mapper;
    Mode mode;
    CertTableModel *model;

    QString cert;
};

#endif // EDITCERTDIALOG_H
