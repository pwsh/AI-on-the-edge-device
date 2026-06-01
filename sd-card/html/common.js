 
/* The UI can also be run locally, but you have to set the IP of your device accordingly.
 * And you also might have to disable CORS in your webbrowser!
 * Eg using https://chromewebstore.google.com/detail/allow-cors-access-control/lhobafahddgcelffkeicbaginigeejlf?utm_source=ext_app_menu on chrome
 * Keep empty to disable using it. Enabling it will break access through a forwared port, see 
 * https://github.com/jomjol/AI-on-the-edge-device/issues/2681 */
var domainname_for_testing = "";
//var domainname_for_testing = "192.168.1.151";


/* Returns the domainname with prepended protocol.
Eg. http://watermeter.fritz.box or http://192.168.1.5 */
function getDomainname(){
    var host = window.location.hostname;
    if (domainname_for_testing != "") {
        console.log("Using pre-defined domainname for testing: " + domainname_for_testing);
        domainname = "http://" + domainname_for_testing
    }
    else
    {
        domainname = window.location.protocol + "//" + host;
        if (window.location.port != "") {
            domainname = domainname + ":" + window.location.port;
        }
    }

    return domainname;
}

function UpdatePage(_dosession = true){
    var zw = location.href;
    zw = zw.substr(0, zw.indexOf("?"));
    if (_dosession) {
        window.location = zw + '?session=' + Math.floor((Math.random() * 1000000) + 1); 
    }
    else {
        window.location = zw; 
    }
}

        
function LoadHostname() {
    _domainname = getDomainname(); 


    var xhttp = new XMLHttpRequest();
    xhttp.addEventListener('load', function(event) {
        if (xhttp.status >= 200 && xhttp.status < 300) {
            hostname = xhttp.responseText;
                document.title = hostname + " - AI on the edge";
                document.getElementById("id_title").innerHTML  = "Digitizer - AI on the edge - " + hostname;
        } 
        else {
                console.warn(request.statusText, request.responseText);
        }
    });

//     var xhttp = new XMLHttpRequest();
    try {
            url = _domainname + '/info?type=Hostname';     
            xhttp.open("GET", url, true);
            xhttp.send();

    }
    catch (error)
    {
//               alert("Loading Hostname failed");
    }
}


var fwVersion = "";
var webUiVersion = "";

function LoadFwVersion() {
    _domainname = getDomainname(); 

    var xhttp = new XMLHttpRequest();
    xhttp.addEventListener('load', function(event) {
        if (xhttp.status >= 200 && xhttp.status < 300) {
            fwVersion = xhttp.responseText;
            document.getElementById("Version").innerHTML  = fwVersion;
            console.log(fwVersion);
            compareVersions();
        } 
        else {
            console.warn(request.statusText, request.responseText);
            fwVersion = "NaN";
        }
    });

    try {
        url = _domainname + '/info?type=FirmwareVersion';     
        xhttp.open("GET", url, true);
        xhttp.send();
    }
    catch (error) {
        fwVersion = "NaN";
    }
}

function LoadWebUiVersion() {
    _domainname = getDomainname(); 

    var xhttp = new XMLHttpRequest();
    xhttp.addEventListener('load', function(event) {
        if (xhttp.status >= 200 && xhttp.status < 300) {
            webUiVersion = xhttp.responseText;
            console.log("Web UI Version: " + webUiVersion);
            compareVersions();
        } 
        else {
            console.warn(request.statusText, request.responseText);
            webUiVersion = "NaN";
        }
    });

    try {
        url = _domainname + '/info?type=HTMLVersion';     
        console.log("url");
        xhttp.open("GET", url, true);
        xhttp.send();
    }
    catch (error) {
        webUiVersion = "NaN";
    }
}


function compareVersions() {
    if (fwVersion == "" || webUiVersion == "") {
        return;
    }

    arr = fwVersion.split(" ");
    fWGitHash = arr[arr.length - 1].substring(0, 7);
    arr = webUiVersion.split(" ");
    webUiHash = arr[arr.length - 1].substring(0, 7);
    console.log("FW Hash: " + fWGitHash + ", Web UI Hash: " + webUiHash);
    
    if (fWGitHash != webUiHash) {
        // Versions out of sync - typically after a firmware-only flash, or a v16->v17 migration where
        // the on-SD Web UI is still the old one. Make this the one-click migration step: the warning
        // carries a link straight to the OTA Update page, where the matching ...__update__*.zip
        // (firmware + Web UI + models) is applied in one shot.
        firework.launch("The web interface (" + webUiHash + ") does not match the firmware (" +
            fWGitHash + "). Apply the matching <b>...__update__*.zip</b> to sync them. " +
            "<a class=\"button\" style=\"margin-left:8px;padding:2px 10px;white-space:nowrap;\" " +
            "onclick=\"gotoOtaUpdate()\">Open the update page →</a>",
            'warning', 60000);
    }
}

// Navigate the main content area to the OTA Update page. Works whether this runs in the index.html
// parent frame (loadPage is global there) or inside a content iframe (use the parent's loadPage).
function gotoOtaUpdate() {
    var page = "ota_page.html?v=$COMMIT_HASH";
    try {
        if (typeof loadPage === "function") { loadPage(page); return; }
        if (window.parent && typeof window.parent.loadPage === "function") { window.parent.loadPage(page); return; }
    } catch (e) { /* cross-origin/frame access - fall through to a plain navigation */ }
    window.location.href = page;
}

// --- Per-sensor camera capabilities ------------------------------------------
// Derived from the esp32-camera drivers (sensors/ov*.c) and the firmware's
// ClassControllCamera.cpp. Used by the config + livestream-setup pages to expose
// only the controls a connected sensor actually supports, and to clamp the zoom
// fields to that sensor's real range (instead of greying things out).
//   unsupported : Cam* settings that are a no-op on this sensor (hide them).
//   zoom        : max |offsetX|, max |offsetY|, max zoom-size (SetZoomSize()).
var CAM_SENSOR_CAPS = {
    "OV2640": { unsupported: ["CamDenoise"],       zoom: { offX: 480, offY: 360, size: 29 } },
    "OV3660": { unsupported: ["CamAutoSharpness"], zoom: { offX: 704, offY: 528, size: 43 } },
    "OV5640": { unsupported: ["CamAutoSharpness"], zoom: { offX: 960, offY: 720, size: 59 } }
};

// Fetch the connected camera model, then call cb(model, caps) where caps is the
// CAM_SENSOR_CAPS entry (or null for an unknown sensor -> caller should show all).
function withCamSensorCaps(cb) {
    var x = new XMLHttpRequest();
    x.onreadystatechange = function () {
        if (this.readyState != 4 || this.status != 200) return;
        var model = (this.responseText || "").trim().toUpperCase();
        cb(model, CAM_SENSOR_CAPS[model] || null);
    };
    try { x.open("GET", getDomainname() + "/info?type=CameraModel", true); x.send(); } catch (e) {}
}

// Set an input's min/max and pull any out-of-range current value back into range.
function camSetRange(id, min, max) {
    var e = document.getElementById(id);
    if (!e) return;
    e.min = min; e.max = max;
    var v = parseInt(e.value, 10);
    if (!isNaN(v)) e.value = Math.max(min, Math.min(max, v));
}
