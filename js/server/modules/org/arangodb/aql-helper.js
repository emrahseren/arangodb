/*jslint indent: 2, nomen: true, maxlen: 100, sloppy: true, vars: true, white: true, plusplus: true */
/*global require, exports, assertTrue, assertEqual, fail */

////////////////////////////////////////////////////////////////////////////////
/// @brief aql test helper functions
///
/// @file
///
/// DISCLAIMER
///
/// Copyright 2011-2012 triagens GmbH, Cologne, Germany
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
/// Copyright holder is triAGENS GmbH, Cologne, Germany
///
/// @author Jan Steemann
/// @author Copyright 2013, triAGENS GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

var internal = require("internal");
var arangodb = require("org/arangodb");
var _ = require("underscore");

// -----------------------------------------------------------------------------
// --SECTION--                                         AQL test helper functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief normalise a single row result 
////////////////////////////////////////////////////////////////////////////////

function normalizeRow (row, recursive) {
  if (row !== null && 
      typeof row === 'object' && 
      ! Array.isArray(row)) {
    var keys = Object.keys(row);

    keys.sort();

    var i, n = keys.length, out = { };
    for (i = 0; i < n; ++i) {
      var key = keys[i];

      if (key[0] !== '_') {
        out[key] = row[key];
      }
    }

    return out;
  }

  if (recursive && Array.isArray(row)) {
    row = row.map(normalizeRow);
  }

  return row;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief return the parse results for a query
////////////////////////////////////////////////////////////////////////////////
    
function getParseResults (query) {
  return internal.AQL_PARSE(query);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief assert a specific error code when parsing a query
////////////////////////////////////////////////////////////////////////////////

function assertParseError (errorCode, query) {
  try {
    getParseResults(query);
    fail();
  }
  catch (e) {
    assertTrue(e.errorNum !== undefined, "unexpected error format");
    assertEqual(errorCode, e.errorNum, "unexpected error code (" + e.errorMessage + "): ");
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @brief return the results of a query explanation
////////////////////////////////////////////////////////////////////////////////

function getQueryExplanation (query, bindVars) {
  var result = internal.AQL_EXPLAIN(query, bindVars);

  return result;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief return the results of a modify-query
////////////////////////////////////////////////////////////////////////////////

function getModifyQueryResults (query, bindVars) {
  var queryResult = internal.AQL_QUERY(query, bindVars); 

  if (queryResult instanceof arangodb.ArangoCursor) {
    return queryResult.getExtra();
  }

  return queryResult.extra;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief return the results of a query
////////////////////////////////////////////////////////////////////////////////

function getRawQueryResults (query, bindVars) {
  var queryResult = internal.AQL_QUERY(query, bindVars, { 
    count: true, 
    batchSize : 3000 
  });

  if (! (queryResult instanceof arangodb.ArangoCursor)) {
    if (typeof queryResult === 'object' && queryResult.hasOwnProperty('docs')) {
      return queryResult.docs;
    }

    return queryResult;
  }

  var rows = [ ];
  var func = function (row) {
    rows.push(row);
  };

  while (queryResult.hasNext()) {
    queryResult.toArray().forEach(func);
  }
  return rows;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief return the results of a query, version for AQL2
////////////////////////////////////////////////////////////////////////////////

function getRawQueryResultsAQL2 (query, bindVars) {
  var queryResult;
  if (typeof bindVars == "object") {
    queryResult = AQL_EXECUTE(query, bindVars, { 
      count: true, 
      batchSize : 3000 
    });
  }
  else {
    queryResult = AQL_EXECUTE(query, {}, {
      count: true,
      batchSize : 3000
    });
  }
  return queryResult.json;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief return the results of a query in a normalised way
////////////////////////////////////////////////////////////////////////////////

function getQueryResults (query, bindVars, recursive) {
  var result = getRawQueryResults(query, bindVars);

  if (Array.isArray(result)) {
    result = result.map(function (row) {
      return normalizeRow(row, recursive);
    });
  }

  return result;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief return the results of a query in a normalised way, AQL2 version
////////////////////////////////////////////////////////////////////////////////

function getQueryResultsAQL2 (query, bindVars, recursive) {
  var result = getRawQueryResultsAQL2(query, bindVars);

  if (Array.isArray(result)) {
    result = result.map(function (row) {
      return normalizeRow(row, recursive);
    });
  }

  return result;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief return the results of a query in a normalised way, version to
/// run both the old and the new (AQL2) query
////////////////////////////////////////////////////////////////////////////////

function getQueryResults2 (query, bindVars, recursive) {
  var result = getQueryResults(query, bindVars, recursive);
  var result2 = getQueryResultsAQL2(query, bindVars, recursive);

  if (! _.isEqual(result, result2)) {
    require("internal").print("Old and new AQL return different results!");
    require("internal").print("Old result:\n", result);
    require("internal").print("New result:\n", result2);

    require("internal").print("Failed Query was:\n", query);

    throw "Results between AQL and AQL2 differ";
  }

  return result;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief assert a specific error code when running a query
////////////////////////////////////////////////////////////////////////////////

function assertQueryError (errorCode, query, bindVars) {
  try {
    getQueryResults(query, bindVars);
    fail();
  }
  catch (e) {
    assertTrue(e.errorNum !== undefined, "unexpected error format");
    assertEqual(errorCode, e.errorNum, "unexpected error code (" + e.errorMessage + "): ");
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @brief assert a specific error code when running a query, case of both
/// new and old AQL
////////////////////////////////////////////////////////////////////////////////

function assertQueryError2 (errorCode, query, bindVars) {
  try {
    getQueryResults(query, bindVars);
    fail();
  }
  catch (e) {
    assertTrue(e.errorNum !== undefined, "unexpected error format");
    assertEqual(errorCode, e.errorNum, "unexpected error code (" + e.errorMessage + "): ");
  }
  try {
    getQueryResultsAQL2(query, bindVars);
    fail();
  }
  catch (e2) {
    assertTrue(e2.errorNum !== undefined, "unexpected error format");
    assertEqual(errorCode, e2.errorNum, "unexpected error code (" + e2.errorMessage + "): ");
  }
}

// -----------------------------------------------------------------------------
// --SECTION--                                                    module exports
// -----------------------------------------------------------------------------

exports.getParseResults       = getParseResults;
exports.assertParseError      = assertParseError;
exports.getQueryExplanation   = getQueryExplanation;
exports.getModifyQueryResults = getModifyQueryResults;
exports.getRawQueryResults    = getRawQueryResults;
exports.getQueryResults       = getQueryResults;
exports.getQueryResults2      = getQueryResults2;
exports.assertQueryError      = assertQueryError;
exports.assertQueryError2     = assertQueryError2;

// -----------------------------------------------------------------------------
// --SECTION--                                                       END-OF-FILE
// -----------------------------------------------------------------------------

// Local Variables:
// mode: outline-minor
// outline-regexp: "^\\(/// @brief\\|/// @addtogroup\\|// --SECTION--\\|/// @page\\|/// @\\}\\)"
// End:
