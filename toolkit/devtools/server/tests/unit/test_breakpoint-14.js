/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * Check that a breakpoint or a debugger statement cause execution to pause even
 * in a stepped-over function.
 */

var gDebuggee;
var gClient;
var gThreadClient;

function run_test()
{
  initTestDebuggerServer();
  gDebuggee = addTestGlobal("test-stack");
  gClient = new DebuggerClient(DebuggerServer.connectPipe());
  gClient.connect(function () {
    attachTestTabAndResume(gClient, "test-stack", function (aResponse, aTabClient, aThreadClient) {
      gThreadClient = aThreadClient;
      test_simple_breakpoint();
    });
  });
  do_test_pending();
}

function test_simple_breakpoint()
{
  gThreadClient.addOneTimeListener("paused", function (aEvent, aPacket) {
    let path = getFilePath('test_breakpoint-14.js');
    let location = { url: path, line: gDebuggee.line0 + 2};
    gThreadClient.setBreakpoint(location, function (aResponse, bpClient) {
      gThreadClient.addOneTimeListener("paused", function (aEvent, aPacket) {
        // Check that the stepping worked.
        do_check_eq(aPacket.frame.where.line, gDebuggee.line0 + 5);
        do_check_eq(aPacket.why.type, "resumeLimit");

        gThreadClient.addOneTimeListener("paused", function (aEvent, aPacket) {
          // Reached the breakpoint.
          do_check_eq(aPacket.frame.where.line, location.line);
          do_check_eq(aPacket.why.type, "breakpoint");
          do_check_neq(aPacket.why.type, "resumeLimit");

          gThreadClient.addOneTimeListener("paused", function (aEvent, aPacket) {
            // The frame is about to be popped while stepping.
            do_check_eq(aPacket.frame.where.line, location.line);
            do_check_neq(aPacket.why.type, "breakpoint");
            do_check_eq(aPacket.why.type, "resumeLimit");
            do_check_eq(aPacket.why.frameFinished.return.type, "undefined");

            gThreadClient.addOneTimeListener("paused", function (aEvent, aPacket) {
              // The foo function call frame was just popped from the stack.
              do_check_eq(gDebuggee.a, 1);
              do_check_eq(gDebuggee.b, undefined);
              do_check_eq(aPacket.frame.where.line, gDebuggee.line0 + 5);
              do_check_eq(aPacket.why.type, "resumeLimit");
              do_check_eq(aPacket.poppedFrames.length, 1);

              gThreadClient.addOneTimeListener("paused", function (aEvent, aPacket) {
                // Check that the debugger statement wasn't the reason for this pause.
                do_check_eq(aPacket.frame.where.line, gDebuggee.line0 + 6);
                do_check_neq(aPacket.why.type, "debuggerStatement");
                do_check_eq(aPacket.why.type, "resumeLimit");

                gThreadClient.addOneTimeListener("paused", function (aEvent, aPacket) {
                  // Check that the debugger statement wasn't the reason for this pause.
                  do_check_eq(aPacket.frame.where.line, gDebuggee.line0 + 7);
                  do_check_neq(aPacket.why.type, "debuggerStatement");
                  do_check_eq(aPacket.why.type, "resumeLimit");

                  // Remove the breakpoint and finish.
                  bpClient.remove(() => gThreadClient.resume(() => finishClient(gClient)));

                });
                // Step past the debugger statement.
                gThreadClient.stepOver();
              });
              // Step over the debugger statement.
              gThreadClient.stepOver();
            });
            // Get back to the frame above.
            gThreadClient.stepOver();
          });
          // Step to the end of the function call frame.
          gThreadClient.stepOver();
        });
        // Step over the function call.
        gThreadClient.stepOver();
      });
      // Step over to the next line with the function call.
      gThreadClient.stepOver();
    });
  });

  gDebuggee.eval("var line0 = Error().lineNumber;\n" +
                 "function foo() {\n" + // line0 + 1
                 "  this.a = 1;\n" +    // line0 + 2 <-- Breakpoint is set here.
                 "}\n" +                // line0 + 3
                 "debugger;\n" +        // line0 + 4
                 "foo();\n" +           // line0 + 5
                 "debugger;\n" +        // line0 + 6
                 "var b = 2;\n");       // line0 + 7
}
