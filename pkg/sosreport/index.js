/*
 * This file is part of Cockpit.
 *
 * Copyright (C) 2015 Red Hat, Inc.
 *
 * Cockpit is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * Cockpit is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with Cockpit; If not, see <http://www.gnu.org/licenses/>.
 */

import cockpit from "cockpit";
import $ from "jquery";
import { superuser } from "superuser";
import { LongRunningProcess, ProcessState } from 'long-running-process';

import '../lib/patternfly/patternfly-cockpit.scss';

const _ = cockpit.gettext;

var sos_task;
var sos_output;
var sos_archive_url;
var sos_archive_files;

function sos_error(message, extra) {
    $("#sos-alert, #sos-progress, #sos-download").hide();
    $("#sos-error .alert-message").text(message);
    if (extra) {
        $("#sos-error-extra").text(extra);
        $("#sos-error-extra").show();
    } else
        $("#sos-error-extra").hide();
    $("#sos-error").show();
    $("#sos-cancel").text(_("Close"));
}

function sos_create() {
    sos_archive_url = null;
    sos_archive_files = [];

    $("#sos-progress .progress-bar").css("width", "0%");
    $("#sos-download, #sos-error").hide();
    $("#sos-cancel").text(_("Cancel"));

    // HACK: SELinux has a pointless and broken exec_t policy, work around it: https://bugzilla.redhat.com/show_bug.cgi?id=1858740
    sos_task.run(["sh", "-c", "exec sosreport --batch"], { /* pty: true */ })
            .catch(error => sos_error(error.toString()));
}

function sos_process_output(text) {
    // TODO - Use a real API instead of scraping stdout once such
    //        an API exists.
    console.log("XXX sos_process_output", text);
    let plugins_count;
    const progress_regex = /Running ([0-9]+)\/([0-9]+):/; // Only for sos < 3.6
    const finishing_regex = /Finishing plugins.*\[Running: (.*)\]/;
    const starting_regex = /Starting ([0-9]+)\/([0-9]+).*\[Running: (.*)\]/;

    let m
    let p = 0;

    sos_output += text;
    const lines = sos_output.split("\n");
    for (let i = lines.length - 1; i >= 0; i--) {
        if ((m = starting_regex.exec(lines[i]))) {
            plugins_count = parseInt(m[2], 10);
            p = ((parseInt(m[1], 10) - m[3].split(" ").length) / plugins_count) * 100;
            break;
        } else if ((m = finishing_regex.exec(lines[i]))) {
            if (!plugins_count)
                p = 100;
            else
                p = ((plugins_count - m[1].split(" ").length) / plugins_count) * 100;
            break;
        } else if ((m = progress_regex.exec(lines[i]))) {
            p = (parseInt(m[1], 10) / parseInt(m[2], 10)) * 100;
            break;
        }
    }
    $("#sos-alert, #sos-progress").show();
    $("#sos-progress .progress-bar").css("width", p.toString() + "%");
}

function sos_done() {
    console.log("XXX sos_done", sos_output);
    const archive_regex = /Your sosreport has been generated and saved in:\s+(\/[^\r\n]+)/;
    const m = archive_regex.exec(sos_output);
    if (m) {
        const archive = m[1];
        const basename = archive.replace(/.*\//, "");

        // When running sosreport in a container on the
        // Atomics, the archive path needs to be adjusted.
        //
        if (archive.indexOf("/host") === 0)
            archive = archive.substr(5);

        sos_archive_files = [archive, archive + ".md5"];

        const query = window.btoa(JSON.stringify({
            payload: "fsread1",
            binary: "raw",
            path: archive,
            superuser: true,
            max_read_size: 150 * 1024 * 1024,
            external: {
                "content-disposition": 'attachment; filename="' + basename + '"',
                "content-type": "application/x-xz, application/octet-stream"
            }
        }));
        const prefix = (new URL(cockpit.transport.uri("channel/" + cockpit.transport.csrf_token))).pathname;
        sos_archive_url = prefix + '?' + query;
        $("#sos-progress, #sos-error").hide();
        $("#sos-alert, #sos-download").show();
        $("#sos-cancel").text(_("Close"));
    } else {
        sos_error(_("No archive has been created."), sos_output);
    }

    sos_output = "";
}

function sos_cancel() {
    if (sos_task)
        sos_task.terminate();

    if (sos_archive_files.length > 0) {
        cockpit.spawn(["rm"].concat(sos_archive_files), { superuser: true, err: "message" })
                .fail(function (error) {
                    console.log("failed to remove", sos_archive_files, error);
                });
    }
    sos_archive_url = null;
    sos_archive_files = [];
    $("#sos").modal('hide');
}

function sos_download() {
    // We download via a hidden iframe to get better control over
    // the error cases.
    var iframe = $('<iframe>').attr('src', sos_archive_url)
            .hide();
    iframe.on('load', function (event) {
        var title = iframe.get(0).contentDocument.title;
        if (title)
            sos_error(title);
    });
    $('body').append(iframe);
}

// follow live output of the given unit, put into "output" <pre> area
function showJournal(unitName, filter_arg) {
    // run at most one instance of journal tailing
    if (showJournal.journalctl)
        return;

    // reset previous output
    output.innerHTML = "";

    const argv = ["journalctl", "--output=cat", "--unit", unitName, "--follow", "--lines=all", filter_arg];
    showJournal.journalctl = cockpit.spawn(argv, { superuser: "require", err: "message" })
            .stream(data => output.append(document.createTextNode(data)))
            .catch(ex => { output.innerHTML = JSON.stringify(ex) });
}

function sos_task_update(process) {
    console.log("XXX sos_task_update", process.state);
    switch (process.state) {
        case ProcessState.INIT:
            break;

        case ProcessState.STOPPED:
            sos_done();
            break;

        case ProcessState.RUNNING:
            sos_output = "";

            // follow live output
            if (!sos_task_update.journalctl) {
                // StateChangeTimestamp property is in µs since epoch, but journalctl expects seconds
                const argv = ["journalctl", "--output=cat", "--unit", process.serviceName, "--follow", "--lines=all", "--since=@" + Math.floor(process.startTimestamp / 1000000)];
                sos_task_update.journalctl = cockpit.spawn(argv, { superuser: "require", err: "message" })
                        .stream(sos_process_output)
                        .catch(error => sos_error(error.toString()));
            }
            break;

        case ProcessState.FAILED:
            sos_error(sos_output);
            break;

        default:
            throw new Error("unexpected process.state: " + process.state);
    }
}

function init() {
    $(function () {
        $("#sos").on("show.bs.modal", sos_create);
        $("#sos-cancel").on("click", sos_cancel);
        $('#sos-download button').on('click', sos_download);

        cockpit.translate();
        $('body').prop("hidden", false);

        function update_admin_allowed() {
            $("#switch-instructions").toggle(superuser.allowed === false);
            $("#create-button").toggle(!!superuser.allowed);
        }

        $(superuser).on("changed", update_admin_allowed);
        update_admin_allowed();

        sos_task = new LongRunningProcess("cockpit-sosreport.service", sos_task_update);

        // Send a 'init' message.  This tells the tests that we
        // are ready to go.
        //
        cockpit.transport.wait(function () { });
    });
}

init();
