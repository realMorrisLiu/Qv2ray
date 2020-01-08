// Supplementary file for MainWindow -- Basically the handler for connectivity management
// and components interactions.
// We NEED to include the cpp file to define the macros.
#include "w_MainWindow.cpp"
#include "QvSystemProxyConfigurator.hpp"

QTreeWidgetItem *MainWindow::FindItemByIdentifier(QvConfigIdentifier identifier)
{
    // First filter out all items with our config name.
    auto items = connectionListWidget->findItems(identifier.connectionName, Qt::MatchExactly | Qt::MatchRecursive);

    for (auto item : items) {
        // This connectable prevents the an item with (which is the parent node of a subscription, having the same
        // -- name as our current connected name)
        if (!IsConnectableItem(item)) {
            LOG(MODULE_UI, "Invalid Item found: " + item->text(0))
            continue;
        }

        auto thisIdentifier = ItemConnectionIdentifier(item);
        DEBUG(MODULE_UI, "Item Identifier: " + thisIdentifier.IdentifierString())

        if (identifier == thisIdentifier) {
            return item;
        }
    }

    LOG(MODULE_UI, "Warning: Failed to find an item named: " + identifier.IdentifierString())
    return nullptr;
}

void MainWindow::MWFindAndStartAutoConfig()
{
    if (!currentConfig.autoStartConfig.connectionName.isEmpty()) {
        // User has auto start configured, we try to find that connection item.
        auto name = currentConfig.autoStartConfig.subscriptionName.isEmpty()
                    ? currentConfig.autoStartConfig.connectionName
                    : currentConfig.autoStartConfig.connectionName + " (" + tr("Subscription:") + " " + currentConfig.autoStartConfig.subscriptionName + ")";
        //
        LOG(MODULE_UI, "Found auto start config: " + name)
        auto item = FindItemByIdentifier(currentConfig.autoStartConfig);

        if (item != nullptr) {
            // We found the item required and start it.
            connectionListWidget->setCurrentItem(item);
            on_connectionListWidget_currentItemChanged(item, nullptr);
            connectionListWidget->scrollToItem(item);
            tray_RootMenu->actions()[0]->setText(tr("Show"));
            on_startButton_clicked();
        } else {
            QvMessageBox(this, tr("Autostarting a config"), tr("Could not find a specified config named: ") + NEWLINE +
                         name + NEWLINE + NEWLINE +
                         tr("Please reset the settings in Preference Window"));
        }
    } else if (connectionListWidget->topLevelItemCount() > 0) {
        // Make the first one our default selected item.
        connectionListWidget->setCurrentItem(connectionListWidget->topLevelItem(0));
        ShowAndSetConnection(ItemConnectionIdentifier(connectionListWidget->topLevelItem(0)), true, false);
    }
}

void MainWindow::MWClearSystemProxy(bool showMessage)
{
    ClearSystemProxy();
    LOG(MODULE_UI, "Clearing System Proxy")
    systemProxyEnabled = false;

    if (showMessage) {
        hTray->showMessage(tr("System Proxy"), tr("System proxy cleared."), windowIcon());
    }
}

void MainWindow::MWSetSystemProxy()
{
    bool usePAC = currentConfig.inboundConfig.pacConfig.enablePAC;
    bool pacUseSocks = currentConfig.inboundConfig.pacConfig.useSocksProxy;
    bool httpEnabled = currentConfig.inboundConfig.useHTTP;
    bool socksEnabled = currentConfig.inboundConfig.useSocks;
    //
    // Set system proxy if necessary
    bool isComplex = CheckIsComplexConfig(connections[CurrentConnectionIdentifier].config);

    if (!isComplex) {
        // Is simple config and we will try to set system proxy.
        LOG(MODULE_UI, "Preparing to set system proxy")
        //
        QString proxyAddress;
        bool canSetSystemProxy = true;

        if (usePAC) {
            if ((httpEnabled && !pacUseSocks) || (socksEnabled && pacUseSocks)) {
                // If we use PAC and socks/http are properly configured for PAC
                LOG(MODULE_PROXY, "System proxy uses PAC")
                proxyAddress = "http://" + currentConfig.inboundConfig.listenip + ":" + QString::number(currentConfig.inboundConfig.pacConfig.port) +  "/pac";
            } else {
                // Not properly configured
                LOG(MODULE_PROXY, "Failed to process pac due to following reasons:")
                LOG(MODULE_PROXY, " --> PAC is configured to use socks but socks is not enabled.")
                LOG(MODULE_PROXY, " --> PAC is configuted to use http but http is not enabled.")
                QvMessageBox(this, tr("PAC Processing Failed"), tr("HTTP or SOCKS inbound is not properly configured for PAC") +
                             NEWLINE + tr("Qv2ray will continue, but will not set system proxy."));
                canSetSystemProxy = false;
            }
        } else {
            // Not using PAC
            if (httpEnabled) {
                // Not use PAC, System proxy should use HTTP
                LOG(MODULE_PROXY, "Using system proxy with HTTP")
                proxyAddress = "localhost";
            } else {
                LOG(MODULE_PROXY, "HTTP is not enabled, cannot set system proxy.")
                QvMessageBox(this, tr("Cannot set system proxy"), tr("HTTP inbound is not enabled"));
                canSetSystemProxy = false;
            }
        }

        if (canSetSystemProxy) {
            LOG(MODULE_UI, "Setting system proxy for simple config, HTTP only")
            // ------------------------|=======We only use HTTP here->>|=======|
            SetSystemProxy(proxyAddress, currentConfig.inboundConfig.http_port, usePAC);
            systemProxyEnabled = true;
            hTray->showMessage(tr("System Proxy"), tr("System proxy settings applied."), windowIcon());
        }
    } else {
        hTray->showMessage(tr("System Proxy"), tr("Cannot set proxy for complex config."), windowIcon());
    }
}

bool MainWindow::MWtryStartConnection()
{
    auto connectionRoot = connections[CurrentConnectionIdentifier].config;
    currentFullConfig = GenerateRuntimeConfig(connectionRoot);
    bool startFlag = this->vinstance->StartConnection(currentFullConfig, currentConfig.connectionConfig.statsPort);

    if (startFlag) {
        bool usePAC = currentConfig.inboundConfig.pacConfig.enablePAC;
        bool pacUseSocks = currentConfig.inboundConfig.pacConfig.useSocksProxy;
        bool httpEnabled = currentConfig.inboundConfig.useHTTP;
        bool socksEnabled = currentConfig.inboundConfig.useSocks;

        if (usePAC) {
            bool canStartPAC = true;
            QString pacProxyString;  // Something like this --> SOCKS5 127.0.0.1:1080; SOCKS 127.0.0.1:1080; DIRECT; http://proxy:8080
            auto pacIP = currentConfig.inboundConfig.pacConfig.localIP;

            if (pacIP.isEmpty()) {
                LOG(MODULE_PROXY, "PAC Local IP is empty, default to 127.0.0.1")
                pacIP = "127.0.0.1";
            }

            if (pacUseSocks) {
                if (socksEnabled) {
                    pacProxyString = "SOCKS5 " + pacIP + ":" + QString::number(currentConfig.inboundConfig.socks_port);
                } else {
                    LOG(MODULE_UI, "PAC is using SOCKS, but it is not enabled")
                    QvMessageBox(this, tr("Configuring PAC"), tr("Could not start PAC server as it is configured to use SOCKS, but it is not enabled"));
                    canStartPAC = false;
                }
            } else {
                if (httpEnabled) {
                    pacProxyString = "PROXY http://" + pacIP + ":" + QString::number(currentConfig.inboundConfig.http_port);
                } else {
                    LOG(MODULE_UI, "PAC is using HTTP, but it is not enabled")
                    QvMessageBox(this, tr("Configuring PAC"), tr("Could not start PAC server as it is configured to use HTTP, but it is not enabled"));
                    canStartPAC = false;
                }
            }

            if (canStartPAC) {
                pacServer->SetProxyString(pacProxyString);
                pacServer->StartListen();
            } else {
                LOG(MODULE_PROXY, "Not starting PAC due to previous error.")
            }
        }

        if (currentConfig.inboundConfig.setSystemProxy) {
            MWSetSystemProxy();
        }
    }

    return startFlag;
}

void MainWindow::MWStopConnection()
{
    if (systemProxyEnabled) {
        MWClearSystemProxy(false);
    }

    this->vinstance->StopConnection();
    QFile(QV2RAY_GENERATED_FILE_PATH).remove();

    if (currentConfig.inboundConfig.pacConfig.enablePAC) {
        pacServer->StopServer();
        LOG(MODULE_UI, "Stopping PAC server")
    }
}

void MainWindow::MWTryPingConnection(const QvConfigIdentifier &alias)
{
    try {
        auto info  = MWGetConnectionInfo(alias);
        QString host = get<0>(info);
        int port = get<1>(info);
        tcpingModel->StartPing(alias, host, port);
    }  catch (...) {
        QvMessageBox(this, tr("Latency Test"), tr("Failed to test latency for this connection."));
    }
}

tuple<QString, int, QString> MainWindow::MWGetConnectionInfo(const QvConfigIdentifier &alias)
{
    if (!connections.contains(alias))
        return make_tuple(tr("N/A"), 0, tr("N/A"));

    return GetConnectionInfo(connections[alias].config);
}

