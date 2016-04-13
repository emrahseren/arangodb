////////////////////////////////////////////////////////////////////////////////
/// DISCLAIMER
///
/// Copyright 2016 ArangoDB GmbH, Cologne, Germany
///
/// Licensed under the Apache License, Version 2.0 (the "License");
/// you may not use this file except in compliance with the License.
/// You may obtain a copy of the License at
///
///     http://www.apache.org/licenses/LICENSE-2.0
///
/// Unless required by applicable law or agreed to in writing, software
/// distributed under the License is distributed on an "AS IS" BASIS,
/// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
/// See the License for the specific language governing permissions and
/// limitations under the License.
///
/// Copyright holder is ArangoDB GmbH, Cologne, Germany
///
/// @author Dr. Frank Celler
////////////////////////////////////////////////////////////////////////////////

#include "ServerFeature.h"

#include "Basics/ArangoGlobalContext.h"
#include "Basics/process-utils.h"
#include "Cluster/HeartbeatThread.h"
#include "Cluster/ServerState.h"
#include "Logger/Logger.h"
#include "ProgramOptions/ProgramOptions.h"
#include "ProgramOptions/Section.h"
#include "Rest/HttpRequest.h"
#include "Rest/Version.h"
#include "RestServer/DatabaseFeature.h"
#include "Scheduler/SchedulerFeature.h"
#include "Statistics/StatisticsFeature.h"
#include "V8/v8-conv.h"
#include "V8/v8-globals.h"
#include "V8/v8-utils.h"
#include "V8Server/V8Context.h"
#include "V8Server/V8DealerFeature.h"

using namespace arangodb;
using namespace arangodb::application_features;
using namespace arangodb::options;
using namespace arangodb::rest;

ServerFeature::ServerFeature(application_features::ApplicationServer* server, int* res)
    : ApplicationFeature(server, "Server"),
      _console(false),
      _restServer(true),
      _authentication(false),
      _result(res),
      _operationMode(OperationMode::MODE_SERVER) {
  setOptional(true);
  requiresElevatedPrivileges(false);
  startsAfter("Cluster");
  startsAfter("Database");
  startsAfter("Dispatcher");
  startsAfter("Scheduler");
  startsAfter("Statistics");
  startsAfter("V8Dealer");
  startsAfter("WorkMonitor");
}

void ServerFeature::collectOptions(std::shared_ptr<ProgramOptions> options) {
  LOG_TOPIC(TRACE, Logger::STARTUP) << name() << "::collectOptions";

  options->addOption("--console", "start a JavaScript emergency console",
                     new BooleanParameter(&_console, false));

  options->addSection("server", "Server features");

  options->addHiddenOption("--server.rest-server", "start a rest-server",
                           new BooleanParameter(&_restServer));

#warning TODO
#if 0
  // other options
      "start-service", "used to start as windows service")

      (
      "", &,
      "");

(
              "server.hide-product-header", &HttpResponse::HIDE_PRODUCT_HEADER,
              "do not expose \"Server: ArangoDB\" header in HTTP responses")

              "server.session-timeout", &VocbaseContext::ServerSessionTtl,
              "timeout of web interface server sessions (in seconds)");


  additional["Server Options:help-admin"](
      "server.authenticate-system-only", &_authenticateSystemOnly,
      "use HTTP authentication only for requests to /_api and /_admin")

    (
      "server.disable-authentication", &_disableAuthentication,
      "disable authentication for ALL client requests")

#ifdef ARANGODB_HAVE_DOMAIN_SOCKETS
      ("server.disable-authentication-unix-sockets",
       &_disableAuthenticationUnixSockets,
       "disable authentication for requests via UNIX domain sockets")
#endif
#endif

  options->addSection("javascript", "Configure the Javascript engine");

  options->addHiddenOption("--javascript.unit-tests", "run unit-tests and exit",
                           new VectorParameter<StringParameter>(&_unitTests));

  options->addOption("--javascript.script", "run scripts and exit",
                     new VectorParameter<StringParameter>(&_scripts));

  options->addOption("--javascript.script-parameter", "script parameter",
                     new VectorParameter<StringParameter>(&_scriptParameters));
}

void ServerFeature::validateOptions(std::shared_ptr<ProgramOptions>) {
  LOG_TOPIC(TRACE, Logger::STARTUP) << name() << "::validateOptions";

  int count = 0;

  if (_console) {
    _operationMode = OperationMode::MODE_CONSOLE;
    ++count;
  }

  if (!_unitTests.empty()) {
    _operationMode = OperationMode::MODE_UNITTESTS;
    ++count;
  }

  if (!_scripts.empty()) {
    _operationMode = OperationMode::MODE_SCRIPT;
    ++count;
  }

  if (1 < count) {
    LOG(FATAL) << "cannot combine '--console', '--javascript.unit-tests' and "
               << "'--javascript.script'";
    FATAL_ERROR_EXIT();
  }

  if (_operationMode == OperationMode::MODE_SERVER && !_restServer) {
    LOG(FATAL) << "need at least '--console', '--javascript.unit-tests' or"
               << "'--javascript.script if rest-server is disabled";
    FATAL_ERROR_EXIT();
  }

  if (!_restServer) {
    ApplicationServer::disableFeatures({"Daemon", "Dispatcher", "Endpoint",
                                        "RestServer", "Scheduler", "Ssl",
                                        "Supervisor"});

    DatabaseFeature* database = dynamic_cast<DatabaseFeature*>(
        ApplicationServer::lookupFeature("Database"));
    database->disableReplicationApplier();

    StatisticsFeature* statistics = dynamic_cast<StatisticsFeature*>(
        ApplicationServer::lookupFeature("Statistics"));
    statistics->disableStatistics();
  }

  V8DealerFeature* v8dealer = dynamic_cast<V8DealerFeature*>(
      ApplicationServer::lookupFeature("V8Dealer"));

  if (_operationMode == OperationMode::MODE_SCRIPT ||
      _operationMode == OperationMode::MODE_UNITTESTS) {
    _authentication = false;
    v8dealer->setMinimumContexts(2);
  } else {
    v8dealer->setMinimumContexts(1);
  }

  if (_operationMode == OperationMode::MODE_CONSOLE) {
    ApplicationServer::disableFeatures({"Daemon", "Supervisor"});
    v8dealer->increaseContexts();
  }

  if (_operationMode == OperationMode::MODE_SERVER ||
      _operationMode == OperationMode::MODE_CONSOLE) {
    ApplicationServer::lookupFeature("Shutdown")->disable();
  }
}

void ServerFeature::start() {
  LOG_TOPIC(TRACE, Logger::STARTUP) << name() << "::start";

  if (_operationMode != OperationMode::MODE_CONSOLE) {
    auto scheduler = dynamic_cast<SchedulerFeature*>(
        ApplicationServer::lookupFeature("Scheduler"));

    if (scheduler != nullptr) {
      scheduler->buildControlCHandler();
    }
  }

  waitForHeartbeat();

  if (!_authentication) {
    LOG(INFO) << "Authentication is turned off";
  }

  LOG(INFO) << "ArangoDB (version " << ARANGODB_VERSION_FULL
            << ") is ready for business. Have fun!";

  *_result = EXIT_SUCCESS;

  if (_operationMode == OperationMode::MODE_UNITTESTS) {
    *_result = runUnitTests();
  } else if (_operationMode == OperationMode::MODE_SCRIPT) {
    *_result = runScript();
  }
}

void ServerFeature::beginShutdown() {
  LOG_TOPIC(TRACE, Logger::STARTUP) << name() << "::shutdown";

  std::string msg =
      ArangoGlobalContext::CONTEXT->binaryName() + " [shutting down]";
  TRI_SetProcessTitle(msg.c_str());
}

void ServerFeature::stop() {
  LOG_TOPIC(TRACE, Logger::STARTUP) << name() << "::stop";
}

void ServerFeature::waitForHeartbeat() {
  if (!ServerState::instance()->isCoordinator()) {
    // waiting for the heartbeart thread is necessary on coordinator only
    return;
  }

  while (true) {
    if (HeartbeatThread::hasRunOnce()) {
      break;
    }
    usleep(100 * 1000);
  }
}

int ServerFeature::runUnitTests() {
  DatabaseFeature* database = dynamic_cast<DatabaseFeature*>(
      ApplicationServer::lookupFeature("Database"));
  V8Context* context =
      V8DealerFeature::DEALER->enterContext(database->vocbase(), true);

  auto isolate = context->_isolate;

  bool ok = false;
  {
    v8::HandleScope scope(isolate);
    v8::TryCatch tryCatch;

    auto localContext = v8::Local<v8::Context>::New(isolate, context->_context);
    localContext->Enter();
    {
      v8::Context::Scope contextScope(localContext);
      // set-up unit tests array
      v8::Handle<v8::Array> sysTestFiles = v8::Array::New(isolate);

      for (size_t i = 0; i < _unitTests.size(); ++i) {
        sysTestFiles->Set((uint32_t)i, TRI_V8_STD_STRING(_unitTests[i]));
      }

      localContext->Global()->Set(TRI_V8_ASCII_STRING("SYS_UNIT_TESTS"),
                                  sysTestFiles);
      localContext->Global()->Set(TRI_V8_ASCII_STRING("SYS_UNIT_TESTS_RESULT"),
                                  v8::True(isolate));

      v8::Local<v8::String> name(
          TRI_V8_ASCII_STRING(TRI_V8_SHELL_COMMAND_NAME));

      // run tests
      auto input = TRI_V8_ASCII_STRING(
          "require(\"@arangodb/testrunner\").runCommandLineTests();");
      TRI_ExecuteJavaScriptString(isolate, localContext, input, name, true);

      if (tryCatch.HasCaught()) {
        if (tryCatch.CanContinue()) {
          std::cerr << TRI_StringifyV8Exception(isolate, &tryCatch);
        } else {
          // will stop, so need for v8g->_canceled = true;
          TRI_ASSERT(!ok);
        }
      } else {
        ok = TRI_ObjectToBoolean(localContext->Global()->Get(
            TRI_V8_ASCII_STRING("SYS_UNIT_TESTS_RESULT")));
      }
    }
    localContext->Exit();
  }

  V8DealerFeature::DEALER->exitContext(context);

  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}

int ServerFeature::runScript() {
  bool ok = false;

  DatabaseFeature* database = dynamic_cast<DatabaseFeature*>(
      ApplicationServer::lookupFeature("Database"));
  V8Context* context =
      V8DealerFeature::DEALER->enterContext(database->vocbase(), true);

  auto isolate = context->_isolate;

  {
    v8::HandleScope globalScope(isolate);

    auto localContext = v8::Local<v8::Context>::New(isolate, context->_context);
    localContext->Enter();
    {
      v8::Context::Scope contextScope(localContext);
      for (auto script : _scripts) {
        bool r = TRI_ExecuteGlobalJavaScriptFile(isolate, script.c_str(), true);

        if (!r) {
          LOG(FATAL) << "cannot load script '" << script << "', giving up";
          FATAL_ERROR_EXIT();
        }
      }

      v8::TryCatch tryCatch;
      // run the garbage collection for at most 30 seconds
      TRI_RunGarbageCollectionV8(isolate, 30.0);

      // parameter array
      v8::Handle<v8::Array> params = v8::Array::New(isolate);

      params->Set(0, TRI_V8_STD_STRING(_scripts[_scripts.size() - 1]));

      for (size_t i = 0; i < _scriptParameters.size(); ++i) {
        params->Set((uint32_t)(i + 1), TRI_V8_STD_STRING(_scriptParameters[i]));
      }

      // call main
      v8::Handle<v8::String> mainFuncName = TRI_V8_ASCII_STRING("main");
      v8::Handle<v8::Function> main = v8::Handle<v8::Function>::Cast(
          localContext->Global()->Get(mainFuncName));

      if (main.IsEmpty() || main->IsUndefined()) {
        LOG(FATAL) << "no main function defined, giving up";
        FATAL_ERROR_EXIT();
      } else {
        v8::Handle<v8::Value> args[] = {params};

        try {
          v8::Handle<v8::Value> result = main->Call(main, 1, args);

          if (tryCatch.HasCaught()) {
            if (tryCatch.CanContinue()) {
              TRI_LogV8Exception(isolate, &tryCatch);
            } else {
              // will stop, so need for v8g->_canceled = true;
              TRI_ASSERT(!ok);
            }
          } else {
            ok = TRI_ObjectToDouble(result) == 0;
          }
        } catch (arangodb::basics::Exception const& ex) {
          LOG(ERR) << "caught exception " << TRI_errno_string(ex.code()) << ": "
                   << ex.what();
          ok = false;
        } catch (std::bad_alloc const&) {
          LOG(ERR) << "caught exception "
                   << TRI_errno_string(TRI_ERROR_OUT_OF_MEMORY);
          ok = false;
        } catch (...) {
          LOG(ERR) << "caught unknown exception";
          ok = false;
        }
      }
    }

    localContext->Exit();
  }

  V8DealerFeature::DEALER->exitContext(context);

  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
