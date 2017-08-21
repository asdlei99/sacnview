// Copyright 2016 Tom Barthel-Steer
// http://www.tomsteer.net
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "universeview.h"
#include "ui_universeview.h"
#include "streamingacn.h"
#include "sacnlistener.h"
#include "preferences.h"
#include "consts.h"
#include "flickerfinderinfoform.h"
#include "logwindow.h"
#include "mdimainwindow.h"
#include "bigdisplay.h"

#include <QFileDialog>
#include <QStandardPaths>
#include <QMessageBox>

QString onlineToString(bool value)
{
    if(value)
        return QString("Online");
    return QString("Offline");
}

QString protocolVerToString(int value)
{
    switch(value)
    {
    case sACNProtocolDraft:
        return QString("Draft");
    case sACNProtocolRelease:
        return QString("Release");
    }

    return QString("Unknown");
}

UniverseView::UniverseView(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::UniverseView)
{
    m_parentWindow = parent; // needed as parent() in qobject world isn't..
    m_selectedAddress = -1;
    m_logger = NULL;
    m_displayDDOnlySource = Preferences::getInstance()->GetDisplayDDOnly();

    ui->setupUi(this);
    connect(ui->universeDisplay, SIGNAL(selectedCellChanged(int)), this, SLOT(selectedAddressChanged(int)));
    connect(ui->universeDisplay, SIGNAL(cellDoubleClick(quint16)), this, SLOT(openBigDisplay(quint16)));

    ui->btnGo->setEnabled(true);
    ui->btnPause->setEnabled(false);
    ui->sbUniverse->setEnabled(true);
    setUiForLoggingState(NOT_LOGGING);
}

UniverseView::~UniverseView()
{
    delete ui;
}

void UniverseView::startListening(int universe)
{
    ui->twSources->setRowCount(0);
    ui->btnGo->setEnabled(false);
    ui->btnPause->setEnabled(true);
    ui->sbUniverse->setEnabled(false);
    m_listener = sACNManager::getInstance()->getListener(universe);
    ui->universeDisplay->setUniverse(universe);

    // Add the existing sources
    for(int i=0; i<m_listener->sourceCount(); i++)
    {
        sourceOnline(m_listener->source(i));
    }

    connect(m_listener.data(), SIGNAL(sourceFound(sACNSource*)), this, SLOT(sourceOnline(sACNSource*)));
    connect(m_listener.data(), SIGNAL(sourceLost(sACNSource*)), this, SLOT(sourceOffline(sACNSource*)));
    connect(m_listener.data(), SIGNAL(sourceChanged(sACNSource*)), this, SLOT(sourceChanged(sACNSource*)));
    connect(m_listener.data(), SIGNAL(levelsChanged()), this, SLOT(levelsChanged()));

    if(ui->sbUniverse->value()!=universe)
        ui->sbUniverse->setValue(universe);
}

void UniverseView::on_btnGo_pressed()
{
    startListening(ui->sbUniverse->value());
}

void UniverseView::sourceChanged(sACNSource *source)
{
    if(!m_listener) return;
    if(!m_sourceToTableRow.contains(source))
    {
        return;
    }

    // Display sources that only transmit 0xdd?
    if (!m_displayDDOnlySource && !source->doing_dmx) { return; }

    int row = m_sourceToTableRow[source];
    ui->twSources->item(row,COL_NAME)->setText(source->name);
    ui->twSources->item(row,COL_NAME)->setBackgroundColor(Preferences::getInstance()->colorForCID(source->src_cid));
    ui->twSources->item(row,COL_CID)->setText(source->cid_string());
    ui->twSources->item(row,COL_PRIO)->setText(QString::number(source->priority));
    if (source->protocol_version == sACNProtocolDraft)
        ui->twSources->item(row,COL_PREVIEW)->setText(tr("N/A"));
    else
        ui->twSources->item(row,COL_PREVIEW)->setText(source->isPreview ? tr("Yes") : tr("No"));
    ui->twSources->item(row,COL_IP)->setText(source->ip.toString());
    ui->twSources->item(row,COL_FPS)->setText(QString::number((source->fps)));
    {
        // Seq Errors
        QLabel* lbl_SEQ_ERR = qobject_cast<QLabel *>(
                    ui->twSources->cellWidget(row,COL_SEQ_ERR)->layout()->itemAt(0)->widget());
        lbl_SEQ_ERR->setText(QString::number(source->seqErr));
    }
    {
        // Jumps
        QLabel* lbl_SEQ_ERR = qobject_cast<QLabel *>(
                    ui->twSources->cellWidget(row,COL_JUMPS)->layout()->itemAt(0)->widget());
        lbl_SEQ_ERR->setText(QString::number(source->jumps));
    }
    if (source->doing_dmx) {
        ui->twSources->item(row,COL_ONLINE)->setText(onlineToString(source->src_valid));
    } else {
        ui->twSources->item(row,COL_ONLINE)->setText(source->src_valid ? tr("No DMX") : onlineToString(source->src_valid));
    }
    ui->twSources->item(row,COL_VER)->setText(protocolVerToString(source->protocol_version));
    ui->twSources->item(row,COL_DD)->setText(source->doing_per_channel ? tr("Yes") : tr("No"));
}

void UniverseView::sourceOnline(sACNSource *source)
{
    if(!m_listener) return;

    // Display sources that only transmit 0xdd?
    if (!m_displayDDOnlySource && !source->doing_dmx) { return; }

    int row = ui->twSources->rowCount();
    ui->twSources->setRowCount(row+1);
    m_sourceToTableRow[source] = row;

    ui->twSources->setItem(row,COL_NAME,    new QTableWidgetItem() );
    ui->twSources->setItem(row,COL_CID,     new QTableWidgetItem() );
    ui->twSources->setItem(row,COL_PRIO,    new QTableWidgetItem() );
    ui->twSources->setItem(row,COL_PREVIEW, new QTableWidgetItem() );
    ui->twSources->setItem(row,COL_IP,      new QTableWidgetItem() );
    ui->twSources->setItem(row,COL_FPS,     new QTableWidgetItem() );

    // Seq errors, with reset
    {
        QWidget* pWidget = new QWidget();
        QHBoxLayout* pLayout = new QHBoxLayout(pWidget);
        pLayout->setAlignment(Qt::AlignCenter);
        pLayout->setContentsMargins(3,0,0,0);

        // Count
        QLabel* lbl_seq = new QLabel();
        lbl_seq->setText(QString::number(0));
        pLayout->addWidget(lbl_seq);

        // Reset Button
        QPushButton* btn_seq = new QPushButton();
        btn_seq->setText(tr("Reset"));
        pLayout->addWidget(btn_seq);

        // Connect button
        connect(btn_seq, &QPushButton::clicked, [=]() {
            source->resetSeqErr();
            this->sourceChanged(source);
        });

        // Display!
        pWidget->setLayout(pLayout);
        ui->twSources->setCellWidget(row,COL_SEQ_ERR, pWidget);
    }

    // Jump counter, with reset
    {
        QWidget* pWidget = new QWidget();
        QHBoxLayout* pLayout = new QHBoxLayout(pWidget);
        pLayout->setAlignment(Qt::AlignCenter);
        pLayout->setContentsMargins(3,0,0,0);

        // Count
        QLabel* lbl_jumps = new QLabel();
        lbl_jumps->setText(QString::number(0));
        pLayout->addWidget(lbl_jumps);

        // Reset Button
        QPushButton* btn_jumps = new QPushButton();
        btn_jumps->setText(tr("Reset"));
        pLayout->addWidget(btn_jumps);

        // Connect button
        connect(btn_jumps, &QPushButton::clicked, [=]() {
            source->resetJumps();
            this->sourceChanged(source);
        });

        pWidget->setLayout(pLayout);
        ui->twSources->setCellWidget(row,COL_JUMPS, pWidget);
    }

    ui->twSources->setItem(row,COL_ONLINE,  new QTableWidgetItem() );
    ui->twSources->setItem(row,COL_VER,     new QTableWidgetItem() );
    ui->twSources->setItem(row,COL_DD,      new QTableWidgetItem() );

    sourceChanged(source);
}

void UniverseView::sourceOffline(sACNSource *source)
{
    if(!m_listener) return;
    sourceChanged(source);
}

void UniverseView::levelsChanged()
{
    if(!m_listener) return;
    if(m_selectedAddress>-1)
        selectedAddressChanged(m_selectedAddress);
}

void UniverseView::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    resizeColumns();
}

void UniverseView::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);

    resizeColumns();
}

void UniverseView::resizeColumns()
{
    // Attempt to resize so all columns fit
    int width = ui->twSources->width();

    int widthUnit = width/14;

    int used = 0;
    for(int i=COL_NAME; i<COL_END; i++)
    {
        switch(i)
        {
        case COL_NAME:
            break;
        case COL_CID:
        case COL_IP:
        case COL_DD:
            ui->twSources->setColumnWidth(i, 2*widthUnit);
            used += 2*widthUnit;
            break;
        default:
            ui->twSources->setColumnWidth(i, widthUnit);
            used += widthUnit;
            break;
        }
    }

    ui->twSources->setColumnWidth(COL_NAME, width-used-5);
}

void UniverseView::setUiForLoggingState(UniverseView::LOG_STATE state)
{
    switch(state)
    {
    case LOGGING:
        ui->btnLogToFile->setText(tr("Stop Log to File"));
        break;

    default:
    case NOT_LOGGING:
        ui->btnLogToFile->setText(tr("Start Log to File"));
        break;
    }
}

void UniverseView::selectedAddressChanged(int address)
{
    ui->teInfo->clear();
    m_selectedAddress = address;
    ui->teInfo->clear();
    if(address<0) return;

    if(!m_listener) return;
    sACNMergedSourceList list = m_listener->mergedLevels();

    QString info;

    info.append(tr("Address : %1\n")
                .arg(address+1));

    if(list[address].winningSource)
    {
        int prio;
        if(list[address].winningSource->doing_per_channel)
            prio = list[address].winningSource->priority_array[address];
        else
            prio = list[address].winningSource->priority;

        info.append(tr("Winning Source : %1 @ %2 (Priority %3)")
                    .arg(list[address].winningSource->name)
                    .arg(Preferences::getInstance()->GetFormattedValue(list[address].level))
                    .arg(prio));
        if(list[address].otherSources.count()>0)
        {
            foreach(sACNSource *source, list[address].otherSources)
            {
                if(source->doing_per_channel)
                    prio = source->priority_array[address];
                else
                    prio = source->priority;
                info.append(tr("\nOther Source : %1 @ %2 (Priority %3)")
                            .arg(source->name)
                            .arg(Preferences::getInstance()->GetFormattedValue(source->level_array[address]))
                            .arg(prio));
            }
        }
    }
    if(!list[address].winningSource)
    {
        info.append(tr("No Sources"));
    }

    ui->teInfo->setPlainText(info);
}

void UniverseView::openBigDisplay(quint16 address)
{
    MDIMainWindow *mainWindow = dynamic_cast<MDIMainWindow *>(m_parentWindow);
    if(!mainWindow) return;
    BigDisplay *w = new BigDisplay(ui->sbUniverse->value(), address, mainWindow);
    mainWindow->showWidgetAsMdiWindow(w);
}

void UniverseView::on_btnPause_pressed()
{
    ui->universeDisplay->pause();
    this->disconnect(m_listener.data());
    ui->btnGo->setEnabled(true);
    ui->btnPause->setEnabled(false);
    ui->sbUniverse->setEnabled(true);
}

void UniverseView::on_btnLogToFile_pressed()
{
    if(!m_logger)
    {
        //Setup dialog box
        QFileDialog dialog(this);
        dialog.setWindowTitle(APP_NAME);
        dialog.setDirectory(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
        dialog.setNameFilter("CSV Files (*.csv)");
        dialog.setDefaultSuffix("csv");
        dialog.setFileMode(QFileDialog::AnyFile);
        dialog.setViewMode(QFileDialog::Detail);
        dialog.setAcceptMode(QFileDialog::AcceptSave);
        if(dialog.exec()) {
            QString saveName = dialog.selectedFiles().at(0);
            if(saveName.isEmpty()) {
                return;
            }
            m_logger = new MergedUniverseLogger();
            m_logger->start(saveName, m_listener);
            setUiForLoggingState(LOGGING);
        }

    }
    else
    {
        m_logger->stop();
        m_logger->deleteLater();
        m_logger = nullptr;

        setUiForLoggingState(NOT_LOGGING);
    }

}

void UniverseView::on_btnStartFlickerFinder_pressed()
{
    if(ui->universeDisplay->flickerFinder())
    {
        ui->universeDisplay->setFlickerFinder(false);
        ui->btnStartFlickerFinder->setText(tr("Start Flicker Finder"));
    }
    else
    {
        if(Preferences::getInstance()->getFlickerFinderShowInfo())
        {
            FlickerFinderInfoForm form;
            int result = form.exec();
            if(!result) return;
        }
        ui->universeDisplay->setFlickerFinder(true);
        ui->btnStartFlickerFinder->setText(tr("Stop Flicker Finder"));
    }
}


void UniverseView::on_btnLogWindow_pressed()
{
    MDIMainWindow *mainWindow = dynamic_cast<MDIMainWindow *>(m_parentWindow);
    if(!mainWindow) return;
    LogWindow *w = new LogWindow(mainWindow);
    w->setUniverse(ui->sbUniverse->value());
    mainWindow->showWidgetAsMdiWindow(w);
}
