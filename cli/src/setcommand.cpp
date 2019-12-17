// Copyright (c) 2019 London Trust Media Incorporated
//
// This file is part of the Private Internet Access Desktop Client.
//
// The Private Internet Access Desktop Client is free software: you can
// redistribute it and/or modify it under the terms of the GNU General Public
// License as published by the Free Software Foundation, either version 3 of
// the License, or (at your option) any later version.
//
// The Private Internet Access Desktop Client is distributed in the hope that
// it will be useful, but WITHOUT ANY WARRANTY; without even the implied
// warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with the Private Internet Access Desktop Client.  If not, see
// <https://www.gnu.org/licenses/>.

#include "common.h"
#line SOURCE_FILE("setcommand.cpp")

#include "setcommand.h"
#include "getcommand.h" // Shared type and value strings
#include "output.h"
#include "cliclient.h"
#include <map>

namespace
{
    // 'set' supports fewer value types than 'get'.
    const std::map<QString, QString> _setSupportedTypes
    {
        {GetSetType::debugLogging, QStringLiteral("Enable or disable debug logging.")},
        {GetSetType::region, QStringLiteral("Select a region (or \"auto\")")}
    };

    // Match the location specified on the command line to the daemon's location
    // list.  Returns the location ID if a match is found (or "auto").
    //
    // If no match is found, returns an empty string.
    QString matchSpecifiedLocation(const DaemonData &data, const QString &location)
    {
        if(location == GetSetValue::locationAuto)
            return GetSetValue::locationAuto;

        const auto &serverLocations = data.locations();

        // This an O(N) lookup, but since we just run once there's no point to
        // building a better representation of the data just to use it once and
        // throw it away.
        for(const auto &pServerLocation : serverLocations)
        {
            if(pServerLocation && location == GetSetValue::getRegionCliName(pServerLocation))
                return pServerLocation->id();
        }

        qWarning() << "No match found for specified location:" << location;
        return {};
    }

    QJsonArray buildRpcArgs(CliClient &client, const QStringList &params)
    {
        Q_ASSERT(params.length() == 3); // Ensured by exec()

        if(params[1] == GetSetType::region)
        {
            // Figure out the actual location ID
            const auto &id = matchSpecifiedLocation(client.connection().data, params[2]);
            if(id.isEmpty())
            {
                errln() << "Unknown region:" << params[2];
                throw Error{HERE, Error::Code::CliInvalidArgs};
            }

            qInfo().nospace() << "Setting location to " << id << " (" << params[2]
                << ")";

            QJsonObject newSettings;
            newSettings.insert(QStringLiteral("location"), id);
            // `set region` reconnects if connected, pass `true` for reconnectIfNeeded
            return {newSettings, true};
        }
        else if(params[1] == GetSetType::debugLogging)
        {
            bool enabled = GetSetValue::parseBooleanParam(params[2]);
            QJsonValue newValue = enabled ? client.connection().settings.getDefaultDebugLogging() : QJsonValue{};
            QJsonObject newSettings;
            newSettings.insert(QStringLiteral("debugLogging"), newValue);
            return {newSettings};
        }

        // Shouldn't happen, exec() already checked the type
        Q_ASSERT(false);
        throw Error{HERE, Error::Code::CliInvalidArgs};
    }
}

void SetCommand::printHelp(const QString &name)
{
    outln() << "usage:" << name << "<type> <value>";
    outln() << "Change settings in the PIA daemon.";
    outln() << "Available types:";
    OutputIndent indent{2};
    for(const auto &type : _setSupportedTypes)
        outln() << "-" << type.first << "-" << type.second;
}

int SetCommand::exec(const QStringList &params, QCoreApplication &app)
{
    if(params.length() != 3)
    {
        errln() << "Usage:" << params[0] << "<type> <value>";
        throw Error{HERE, Error::Code::CliInvalidArgs};
    }

    if(_setSupportedTypes.count(params[1]) == 0)
    {
        errln() << "Unknown type:" << params[1];
        throw Error{HERE, Error::Code::CliInvalidArgs};
    }

    // 'set' isn't implemented with a one-shot RPC because we need the daemon
    // state to validate the location choice before creating the RPC payload
    CliClient client;
    CliTimeout timeout{app};
    QObject localConnState{};

    Async<void> setRpcResult;

    QObject::connect(&client, &CliClient::firstConnected, &localConnState, [&]()
    {
        // Can't throw across a Qt signal invocation
        try
        {
            QJsonArray rpcArgs = buildRpcArgs(client, params);
            setRpcResult = client.connection().call(QStringLiteral("applySettings"), {rpcArgs})
                ->next(&localConnState, [&app, this](const Error &error, const QJsonValue &)
                {
                    if(error)
                    {
                        app.exit(traceRpcError(error));
                    }
                    else
                    {
                        // Success
                        qInfo() << "Setting change succeeded";
                        app.exit(CliExitCode::Success);
                    }
                });
        }
        catch(const Error &error)
        {
            qWarning() << "Failing with error:" << error;
            // Most of these already printed a message in buildRpcArgs()
            app.exit(mapErrorCode(error.code()));
        }
    });

    return app.exec();
}
