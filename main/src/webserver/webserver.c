#include "webserver.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_wifi.h"
#include "wifi.h"
#include "config.h"
#include "maybom.h"
#include "doamdat.h"
#include "firebase.h"

static const char *TAG = "WEBSERVER";

static httpd_handle_t s_server = NULL;
static SemaphoreHandle_t s_mutex = NULL;
static webserver_pump_mode_t s_pump_mode = WEBSERVER_PUMP_AUTO;

static webserver_log_entry_t s_logs[WEBSERVER_MAX_LOGS];
static int s_log_start = 0;
static int s_log_count = 0;

static webserver_log_entry_t s_current = {
    .time_str = "--:--:--",
    .light_percent = -1,
    .soil_moisture_percent = -1,
    .soil_adc_raw = -1,
    .soil_calibrated = false,
    .water_level_percent = -1,
    .mq2_signal_percent = -1,
    .temperature_c = 0.0f,
    .humidity_percent = 0.0f,
    .dht11_valid = false,
    .pump_is_on = false
};

static const char s_index_html[] =
"<!DOCTYPE html>"
"<html lang=\"vi\">"
"<head>"
"<meta charset=\"UTF-8\">"
"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1.0\">"
"<title>SmartPlan - Giám Sát Cảm Biến Nhà Kính</title>"
"<style>"
":root{--bg:#0f172a;--card:#1e293b;--card-border:#334155;--text:#f8fafc;--muted:#94a3b8;"
"--primary:#38bdf8;--success:#22c55e;--warning:#f59e0b;--danger:#ef4444;}"
"*{margin:0;padding:0;box-sizing:border-box;font-family:system-ui,-apple-system,sans-serif}"
"body{background:var(--bg);color:var(--text);padding:16px;max-width:1100px;margin:0 auto;line-height:1.5}"
"header{display:flex;flex-wrap:wrap;justify-content:space-between;align-items:center;padding:14px 20px;"
"background:var(--card);border:1px solid var(--card-border);border-radius:12px;margin-bottom:20px}"
".title{font-size:1.35rem;font-weight:700;display:flex;align-items:center;gap:8px}"
".status{font-size:0.88rem;color:var(--muted);display:flex;align-items:center;gap:12px}"
".badge-live{display:inline-block;width:10px;height:10px;background:var(--success);border-radius:50%;"
"box-shadow:0 0 8px var(--success);animation:pulse 2s infinite}"
"@keyframes pulse{0%{opacity:1}50%{opacity:0.4}100%{opacity:1}}"
".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(210px,1fr));gap:14px;margin-bottom:24px}"
".card{background:var(--card);border:1px solid var(--card-border);border-radius:12px;padding:16px;"
"display:flex;flex-direction:column;justify-content:space-between;box-shadow:0 4px 6px rgba(0,0,0,0.2)}"
".card-header{font-size:0.85rem;color:var(--muted);margin-bottom:6px;display:flex;justify-content:space-between;align-items:center}"
".card-val{font-size:1.75rem;font-weight:700;color:var(--primary);margin-bottom:4px}"
".card-sub{font-size:0.8rem;color:var(--muted)}"
".tag{font-size:0.75rem;padding:2px 8px;border-radius:999px;font-weight:600}"
".tag-ok{background:rgba(34,197,94,0.15);color:var(--success)}"
".tag-warn{background:rgba(245,158,11,0.15);color:var(--warning)}"
".tag-danger{background:rgba(239,68,68,0.15);color:var(--danger)}"
".tag-off{background:rgba(148,163,184,0.15);color:var(--muted)}"
".panel{background:var(--card);border:1px solid var(--card-border);border-radius:12px;padding:16px 20px;margin-bottom:20px}"
".panel-header{display:flex;flex-wrap:wrap;justify-content:space-between;align-items:center;margin-bottom:14px;gap:10px}"
".panel-title{font-size:1.15rem;font-weight:700;display:flex;align-items:center;gap:8px}"
".panel-tools{display:flex;gap:10px;align-items:center;font-size:0.88rem}"
"button{cursor:pointer;border:none;background:var(--primary);color:#0f172a;font-weight:600;padding:6px 14px;"
"border-radius:6px;transition:0.2s}"
"button:hover{opacity:0.9}"
"button.btn-clear{background:#475569;color:#fff}"
"button.btn-clear:hover{background:var(--danger)}"
".table-wrap{overflow-x:auto;max-height:420px;border-radius:8px;border:1px solid var(--card-border)}"
"table{width:100%;border-collapse:collapse;font-size:0.85rem;text-align:center}"
"th{background:#283548;padding:10px 8px;font-weight:600;position:sticky;top:0;z-index:2}"
"td{padding:8px 6px;border-bottom:1px solid var(--card-border)}"
"tr:nth-child(even){background:rgba(255,255,255,0.02)}"
"tr:hover{background:rgba(56,189,248,0.06)}"
".empty-state{text-align:center;padding:30px;color:var(--muted)}"
"</style>"
"</head>"
"<body>"
"<header>"
"<div class=\"title\">🌱 SmartPlan Greenhouse Monitor</div>"
"<div class=\"status\">"
"<span><i class=\"badge-live\"></i> Trực tiếp</span>"
"<span id=\"last-update\">Cập nhật: --</span>"
"</div>"
"</header>"
""
"<div class=\"grid\">"
"<div class=\"card\">"
"<div class=\"card-header\"><span>🌡️ Nhiệt Độ (DHT11)</span><span id=\"tag-temp\" class=\"tag tag-ok\">DHT11</span></div>"
"<div class=\"card-val\" id=\"val-temp\">-- °C</div>"
"<div class=\"card-sub\">Chân tín hiệu: GPIO17</div>"
"</div>"
"<div class=\"card\">"
"<div class=\"card-header\"><span>💧 Độ Ẩm Không Khí</span><span id=\"tag-hum\" class=\"tag tag-ok\">DHT11</span></div>"
"<div class=\"card-val\" id=\"val-hum\">-- %</div>"
"<div class=\"card-sub\">Chân tín hiệu: GPIO17</div>"
"</div>"
"<div class=\"card\">"
"<div class=\"card-header\"><span>🌱 Độ Ẩm Đất</span><span id=\"tag-soil\" class=\"tag tag-off\">--</span></div>"
"<div class=\"card-val\" id=\"val-soil\">-- %</div>"
"<div class=\"card-sub\" id=\"val-soil-raw\">Raw: --/4095</div>"
"<div class=\"card-sub\" id=\"val-soil-cal-info\" style=\"margin-bottom:6px;\">Mốc: Khô: -- | Ướt: --</div>"
"<div style=\"display:flex;gap:4px;\">"
"<button style=\"background:#0284c7;color:#fff;padding:4px 4px;font-size:0.72rem;flex:1;border-radius:4px;\" onclick=\"calSoil('dry')\">Lưu Khô</button>"
"<button style=\"background:#0284c7;color:#fff;padding:4px 4px;font-size:0.72rem;flex:1;border-radius:4px;\" onclick=\"calSoil('wet')\">Lưu Ướt</button>"
"<button style=\"background:#475569;color:#fff;padding:4px 4px;font-size:0.72rem;flex:1;border-radius:4px;\" onclick=\"calSoil('default')\">Mặc Định</button>"
"</div>"
"</div>"
"<div class=\"card\">"
"<div class=\"card-header\"><span>🌊 Mực Nước</span><span class=\"tag tag-ok\">ADC1_CH6</span></div>"
"<div class=\"card-val\" id=\"val-water\">-- %</div>"
"<div class=\"card-sub\">Cảm biến mực sâu nước</div>"
"</div>"
"<div class=\"card\">"
"<div class=\"card-header\"><span>☀️ Quang Trở</span><span class=\"tag tag-ok\">ADC1_CH4</span></div>"
"<div class=\"card-val\" id=\"val-light\">-- %</div>"
"<div class=\"card-sub\">Cường độ ánh sáng</div>"
"</div>"
"<div class=\"card\">"
"<div class=\"card-header\"><span>⚠️ Khí Gas (MQ-2)</span><span id=\"tag-gas\" class=\"tag tag-ok\">An toàn</span></div>"
"<div class=\"card-val\" id=\"val-gas\">-- %</div>"
"<div class=\"card-sub\">ADC1_CH7 (GPIO35)</div>"
"</div>"
"<div class=\"card\">"
"<div class=\"card-header\"><span>🚰 Máy Bơm Nước</span><span id=\"tag-pump\" class=\"tag tag-off\">TẮT</span></div>"
"<div class=\"card-val\" id=\"val-pump\">TẮT</div>"
"<div class=\"card-sub\" id=\"mode-pump\" style=\"margin-bottom:8px;\">Chế độ: Tự động</div>"
"<div style=\"display:flex;gap:6px;\">"
"<button id=\"btn-pump-on\" style=\"background:var(--success);padding:5px 6px;font-size:0.78rem;flex:1;border-radius:6px;\" onclick=\"setPump('on')\">⚡ Bật</button>"
"<button id=\"btn-pump-off\" style=\"background:#ef4444;color:#fff;padding:5px 6px;font-size:0.78rem;flex:1;border-radius:6px;\" onclick=\"setPump('off')\">⛔ Tắt</button>"
"<button id=\"btn-pump-auto\" style=\"background:#475569;color:#fff;padding:5px 6px;font-size:0.78rem;flex:1;border-radius:6px;\" onclick=\"setPump('auto')\">🔄 Auto</button>"
"</div>"
"</div>"
"</div>"
""
"<div class=\"panel\">"
"<div class=\"panel-header\">"
"<div class=\"panel-title\">📋 Nhật Ký Đo Cảm Biến Gần Nhất</div>"
"<div class=\"panel-tools\">"
"<label style=\"display:flex;align-items:center;gap:6px;cursor:pointer;\">"
"<input type=\"checkbox\" id=\"auto-reload\" checked> Tự cập nhật (3s)"
"</label>"
"<button onclick=\"fetchData()\">Làm mới</button>"
"<button class=\"btn-clear\" onclick=\"clearLogs()\">Xóa log</button>"
"</div>"
"</div>"
"<div class=\"table-wrap\">"
"<table>"
"<thead>"
"<tr>"
"<th>STT</th>"
"<th>Thời gian</th>"
"<th>Nhiệt độ</th>"
"<th>Độ ẩm KK</th>"
"<th>Độ ẩm đất</th>"
"<th>Raw đất</th>"
"<th>Mực nước</th>"
"<th>Ánh sáng</th>"
"<th>Khí gas MQ-2</th>"
"<th>Máy bơm</th>"
"</tr>"
"</thead>"
"<tbody id=\"log-table-body\">"
"<tr><td colspan=\"10\" class=\"empty-state\">Đang tải dữ liệu cảm biến...</td></tr>"
"</tbody>"
"</table>"
"</div>"
"</div>"
""
"<script>"
"function formatVal(v,unit){return (v>=0)?v+unit:'--';}"
"function formatUptime(sec){"
"if(!sec)return '';"
"const h=Math.floor(sec/3600);const m=Math.floor((sec%3600)/60);const s=sec%60;"
"return ' | Chạy: '+(h>0?h+'h ':'')+(m>0?m+'m ':'')+s+'s';"
"}"
"function fetchData(){"
"fetch('/api/data')"
".then(res=>res.json())"
".then(d=>{"
"document.getElementById('last-update').innerText='Cập nhật: '+(d.current.t||'--')+formatUptime(d.current.uptime);"
"if(d.current.dht){"
"document.getElementById('val-temp').innerText=d.current.temp.toFixed(1)+' °C';"
"document.getElementById('val-hum').innerText=d.current.hum.toFixed(1)+' %';"
"document.getElementById('tag-temp').className='tag tag-ok';document.getElementById('tag-temp').innerText='OK';"
"document.getElementById('tag-hum').className='tag tag-ok';document.getElementById('tag-hum').innerText='OK';"
"}else{"
"document.getElementById('val-temp').innerText='Lỗi / Đang chờ';"
"document.getElementById('val-hum').innerText='Lỗi / Đang chờ';"
"document.getElementById('tag-temp').className='tag tag-danger';document.getElementById('tag-temp').innerText='LỖI';"
"document.getElementById('tag-hum').className='tag tag-danger';document.getElementById('tag-hum').innerText='LỖI';"
"}"
"if(d.current.soil>=0){"
"document.getElementById('val-soil').innerText=d.current.soil+' %';"
"document.getElementById('tag-soil').className=d.current.soil_cal?'tag tag-ok':'tag tag-warn';"
"document.getElementById('tag-soil').innerText=d.current.soil_cal?'Đã hiệu chuẩn':'Tự động chuẩn';"
"}else{"
"document.getElementById('val-soil').innerText='Lỗi ADC';"
"document.getElementById('tag-soil').className='tag tag-danger';document.getElementById('tag-soil').innerText='Lỗi cảm biến';"
"}"
"document.getElementById('val-soil-raw').innerText='Raw: '+(d.current.soil_raw>=0?d.current.soil_raw:'--')+'/4095';"
"document.getElementById('val-soil-cal-info').innerText='Mốc: Khô '+(d.current.soil_dry>=0?d.current.soil_dry:'--')+' | Ướt '+(d.current.soil_wet>=0?d.current.soil_wet:'--');"
"document.getElementById('val-water').innerText=formatVal(d.current.water,' %');"
"document.getElementById('val-light').innerText=formatVal(d.current.light,' %');"
"document.getElementById('val-gas').innerText=formatVal(d.current.gas,' %');"
"if(d.current.gas>25){"
"document.getElementById('tag-gas').className='tag tag-danger';document.getElementById('tag-gas').innerText='CẢNH BÁO';"
"}else{"
"document.getElementById('tag-gas').className='tag tag-ok';document.getElementById('tag-gas').innerText='An toàn';"
"}"
"if(d.current.pump){"
"document.getElementById('val-pump').innerText='BẬT';"
"document.getElementById('tag-pump').className='tag tag-ok';document.getElementById('tag-pump').innerText='BẬT';"
"}else{"
"document.getElementById('val-pump').innerText='TẮT';"
"document.getElementById('tag-pump').className='tag tag-off';document.getElementById('tag-pump').innerText='TẮT';"
"}"
"let modeTxt='Tự động (Bật < 30%, Tắt ≥ 45%)';"
"if(d.current.pump_mode===1) modeTxt='Thủ công Web (BẬT)';"
"else if(d.current.pump_mode===2) modeTxt='Thủ công Web (TẮT)';"
"document.getElementById('mode-pump').innerText='Chế độ: '+modeTxt;"
"document.getElementById('btn-pump-on').style.boxShadow = (d.current.pump_mode===1)?'0 0 0 2px #fff':'none';"
"document.getElementById('btn-pump-off').style.boxShadow = (d.current.pump_mode===2)?'0 0 0 2px #fff':'none';"
"document.getElementById('btn-pump-auto').style.boxShadow = (d.current.pump_mode===0)?'0 0 0 2px var(--primary)':'none';"
"const tbody=document.getElementById('log-table-body');"
"if(!d.logs||d.logs.length===0){"
"tbody.innerHTML='<tr><td colspan=\"10\" class=\"empty-state\">Chưa có bản ghi nào</td></tr>';"
"return;"
"}"
"let rows='';"
"for(let i=d.logs.length-1;i>=0;i--){"
"const l=d.logs[i];"
"const tStr=l.dht?(l.temp.toFixed(1)+'°C'):'Lỗi';"
"const hStr=l.dht?(l.hum.toFixed(1)+'%'):'Lỗi';"
"const sStr=(l.soil>=0)?(l.soil+'%'):(l.soil_cal?'--':'Chưa HC');"
"const pBadge=l.pump?'<span class=\"tag tag-ok\">BẬT</span>':'<span class=\"tag tag-off\">TẮT</span>';"
"const gBadge=(l.gas>25)?'<span style=\"color:var(--danger);font-weight:bold\">'+l.gas+'% ⚠️</span>':l.gas+'%';"
"rows+='<tr>'+"
"'<td>'+(i+1)+'</td>'+"
"'<td><b>'+l.t+'</b></td>'+"
"'<td>'+tStr+'</td>'+"
"'<td>'+hStr+'</td>'+"
"'<td>'+sStr+'</td>'+"
"'<td>'+(l.soil_raw>=0?l.soil_raw:'--')+'</td>'+"
"'<td>'+(l.water>=0?l.water+'%':'--')+'</td>'+"
"'<td>'+(l.light>=0?l.light+'%':'--')+'</td>'+"
"'<td>'+gBadge+'</td>'+"
"'<td>'+pBadge+'</td>'+"
"'</tr>';"
"}"
"tbody.innerHTML=rows;"
"})"
".catch(e=>{console.error('Lỗi tải dữ liệu:',e);});"
"}"
"function clearLogs(){"
"if(confirm('Bạn có chắc muốn xóa lịch sử log cảm biến?')){"
"fetch('/api/clear',{method:'POST'})"
".then(()=>fetchData());"
"}"
"}"
"function setPump(act){"
"fetch('/api/pump?action='+act,{method:'POST'})"
".then(res=>res.json())"
".then(()=>fetchData())"
".catch(e=>console.error(e));"
"}"
"function calSoil(act){"
"if(act==='dry'&&!confirm('Luu moc KHO (0%) voi ADC raw hien tai?'))return;"
"if(act==='wet'&&!confirm('Luu moc UOT (100%) voi ADC raw hien tai?'))return;"
"if(act==='default'&&!confirm('Dat moc MAC DINH (Kho=3200, Uot=1400)?'))return;"
"fetch('/api/soil_cal?action='+act,{method:'POST'})"
".then(res=>res.json())"
".then(d=>{alert(d.msg||'Cap nhat thanh cong!');fetchData();})"
".catch(e=>console.error(e));"
"}"
"fetchData();"
"setInterval(()=>{if(document.getElementById('auto-reload').checked)fetchData();},3000);"
"</script>"
"</body>"
"</html>";

static esp_err_t index_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, s_index_html, sizeof(s_index_html) - 1);
}

static esp_err_t api_data_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    if (s_mutex == NULL || xSemaphoreTake(s_mutex, pdMS_TO_TICKS(200)) != pdTRUE) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    uint32_t uptime_sec = (uint32_t)(esp_timer_get_time() / 1000000ULL);

    int soil_dry = -1;
    int soil_wet = -1;
    bool is_cal = false;
    doamdat_get_calibration(&soil_dry, &soil_wet, &is_cal);

    char buf[384];
    int len = snprintf(buf, sizeof(buf),
        "{\"current\":{\"t\":\"%s\",\"uptime\":%lu,\"temp\":%.1f,\"hum\":%.1f,\"dht\":%d,"
        "\"soil\":%d,\"soil_raw\":%d,\"soil_cal\":%d,\"soil_dry\":%d,\"soil_wet\":%d,"
        "\"water\":%d,\"light\":%d,\"gas\":%d,\"pump\":%d,\"pump_mode\":%d},\"logs\":[",
        s_current.time_str,
        (unsigned long)uptime_sec,
        s_current.temperature_c,
        s_current.humidity_percent,
        s_current.dht11_valid ? 1 : 0,
        s_current.soil_moisture_percent,
        s_current.soil_adc_raw,
        s_current.soil_calibrated ? 1 : 0,
        soil_dry,
        soil_wet,
        s_current.water_level_percent,
        s_current.light_percent,
        s_current.mq2_signal_percent,
        s_current.pump_is_on ? 1 : 0,
        (int)s_pump_mode
    );

    esp_err_t res = httpd_resp_send_chunk(req, buf, len);
    if (res != ESP_OK) {
        xSemaphoreGive(s_mutex);
        return res;
    }

    for (int i = 0; i < s_log_count; i++) {
        int idx = (s_log_start + i) % WEBSERVER_MAX_LOGS;
        webserver_log_entry_t *entry = &s_logs[idx];

        len = snprintf(buf, sizeof(buf),
            "%s{\"t\":\"%s\",\"temp\":%.1f,\"hum\":%.1f,\"dht\":%d,"
            "\"soil\":%d,\"soil_raw\":%d,\"soil_cal\":%d,\"water\":%d,\"light\":%d,\"gas\":%d,\"pump\":%d}",
            (i > 0) ? "," : "",
            entry->time_str,
            entry->temperature_c,
            entry->humidity_percent,
            entry->dht11_valid ? 1 : 0,
            entry->soil_moisture_percent,
            entry->soil_adc_raw,
            entry->soil_calibrated ? 1 : 0,
            entry->water_level_percent,
            entry->light_percent,
            entry->mq2_signal_percent,
            entry->pump_is_on ? 1 : 0
        );
        res = httpd_resp_send_chunk(req, buf, len);
        if (res != ESP_OK) {
            xSemaphoreGive(s_mutex);
            return res;
        }
    }

    xSemaphoreGive(s_mutex);

    httpd_resp_send_chunk(req, "]}", 2);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t api_clear_handler(httpd_req_t *req)
{
    webserver_clear_logs();
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
}

static esp_err_t api_pump_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    char action[16] = {0};
    char query[64] = {0};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        httpd_query_key_value(query, "action", action, sizeof(action));
    }

    if (action[0] == '\0' && req->content_len > 0 && req->content_len < 64) {
        char body[64] = {0};
        int ret = httpd_req_recv(req, body, req->content_len);
        if (ret > 0) {
            if (strstr(body, "on")) strcpy(action, "on");
            else if (strstr(body, "off")) strcpy(action, "off");
            else if (strstr(body, "auto")) strcpy(action, "auto");
        }
    }

    if (strcmp(action, "on") == 0) {
        webserver_set_pump_mode(WEBSERVER_PUMP_MANUAL_ON);
        maybom_on(MAYBOM_PIN);
        firebase_set_pump_control(false, true);
        ESP_LOGI(TAG, "Lenh Webserver: BAT BOM (D21 / GPIO%d)", MAYBOM_PIN);
    } else if (strcmp(action, "off") == 0) {
        webserver_set_pump_mode(WEBSERVER_PUMP_MANUAL_OFF);
        maybom_off(MAYBOM_PIN);
        firebase_set_pump_control(false, false);
        ESP_LOGI(TAG, "Lenh Webserver: TAT BOM (D21 / GPIO%d)", MAYBOM_PIN);
    } else if (strcmp(action, "auto") == 0) {
        webserver_set_pump_mode(WEBSERVER_PUMP_AUTO);
        firebase_set_pump_control(true, false);
        ESP_LOGI(TAG, "Lenh Webserver: CHUYEN BOM VE CHE DO TU DONG");
    }

    webserver_pump_mode_t cur_mode = webserver_get_pump_mode();
    char resp[128];
    snprintf(resp, sizeof(resp), "{\"status\":\"ok\",\"action\":\"%s\",\"pump_mode\":%d}", action, (int)cur_mode);
    return httpd_resp_sendstr(req, resp);
}

static esp_err_t api_soil_cal_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    char action[16] = {0};
    char query[64] = {0};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        httpd_query_key_value(query, "action", action, sizeof(action));
    }

    if (action[0] == '\0' && req->content_len > 0 && req->content_len < 64) {
        char body[64] = {0};
        int ret = httpd_req_recv(req, body, req->content_len);
        if (ret > 0) {
            if (strstr(body, "dry")) strcpy(action, "dry");
            else if (strstr(body, "wet")) strcpy(action, "wet");
            else if (strstr(body, "default")) strcpy(action, "default");
            else if (strstr(body, "reset")) strcpy(action, "reset");
        }
    }

    esp_err_t err = ESP_OK;
    const char *msg = "Thanh cong";
    if (strcmp(action, "dry") == 0) {
        err = doamdat_calibrate_dry();
        msg = (err == ESP_OK) ? "Da luu moc KHO thanh cong!" : "Loi luu moc KHO (chua doc duoc mau ADC)";
    } else if (strcmp(action, "wet") == 0) {
        err = doamdat_calibrate_wet();
        msg = (err == ESP_OK) ? "Da luu moc UOT thanh cong!" : "Loi luu moc UOT (2 moc qua gan nhau)";
    } else if (strcmp(action, "default") == 0) {
        err = doamdat_set_default_calibration();
        msg = (err == ESP_OK) ? "Da cai dat moc MAC DINH (Kho=3200, Uot=1400)!" : "Loi luu moc vao NVS";
    } else if (strcmp(action, "reset") == 0) {
        err = doamdat_reset_calibration();
        msg = "Da xoa cac moc hieu chuan!";
    }

    int dry = -1, wet = -1;
    bool cal = false;
    doamdat_get_calibration(&dry, &wet, &cal);

    char resp[256];
    snprintf(resp, sizeof(resp),
             "{\"status\":\"%s\",\"msg\":\"%s\",\"dry\":%d,\"wet\":%d,\"calibrated\":%s}",
             (err == ESP_OK) ? "ok" : "error", msg, dry, wet, cal ? "true" : "false");
    return httpd_resp_sendstr(req, resp);
}

static const httpd_uri_t uri_index = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = index_get_handler,
    .user_ctx  = NULL
};

static const httpd_uri_t uri_api_data = {
    .uri       = "/api/data",
    .method    = HTTP_GET,
    .handler   = api_data_get_handler,
    .user_ctx  = NULL
};

static const httpd_uri_t uri_api_clear_post = {
    .uri       = "/api/clear",
    .method    = HTTP_POST,
    .handler   = api_clear_handler,
    .user_ctx  = NULL
};

static const httpd_uri_t uri_api_clear_get = {
    .uri       = "/api/clear",
    .method    = HTTP_GET,
    .handler   = api_clear_handler,
    .user_ctx  = NULL
};

static const httpd_uri_t uri_api_pump_post = {
    .uri       = "/api/pump",
    .method    = HTTP_POST,
    .handler   = api_pump_handler,
    .user_ctx  = NULL
};

static const httpd_uri_t uri_api_pump_get = {
    .uri       = "/api/pump",
    .method    = HTTP_GET,
    .handler   = api_pump_handler,
    .user_ctx  = NULL
};

static const httpd_uri_t uri_api_soil_cal_post = {
    .uri       = "/api/soil_cal",
    .method    = HTTP_POST,
    .handler   = api_soil_cal_handler,
    .user_ctx  = NULL
};

static const httpd_uri_t uri_api_soil_cal_get = {
    .uri       = "/api/soil_cal",
    .method    = HTTP_GET,
    .handler   = api_soil_cal_handler,
    .user_ctx  = NULL
};

esp_err_t webserver_start(void)
{
    if (s_server != NULL) {
        return ESP_OK;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.ctrl_port = ESP_HTTPD_DEF_CTRL_PORT;
    config.max_uri_handlers = 12;
    config.stack_size = 5120;
    config.lru_purge_enable = true;

    ESP_LOGI(TAG, "Khoi dong HTTP Webserver tren cong %d...", config.server_port);
    esp_err_t ret = httpd_start(&s_server, &config);
    if (ret == ESP_OK) {
        httpd_register_uri_handler(s_server, &uri_index);
        httpd_register_uri_handler(s_server, &uri_api_data);
        httpd_register_uri_handler(s_server, &uri_api_clear_post);
        httpd_register_uri_handler(s_server, &uri_api_clear_get);
        httpd_register_uri_handler(s_server, &uri_api_pump_post);
        httpd_register_uri_handler(s_server, &uri_api_pump_get);
        httpd_register_uri_handler(s_server, &uri_api_soil_cal_post);
        httpd_register_uri_handler(s_server, &uri_api_soil_cal_get);
        ESP_LOGI(TAG, "Webserver da chay thanh cong!");
    } else {
        ESP_LOGE(TAG, "Khong the khoi dong Webserver: %s", esp_err_to_name(ret));
    }
    return ret;
}

void webserver_stop(void)
{
    if (s_server != NULL) {
        ESP_LOGI(TAG, "Dung HTTP Webserver...");
        httpd_stop(s_server);
        s_server = NULL;
    }
}

bool webserver_is_running(void)
{
    return (s_server != NULL);
}

void webserver_record_sensor_data(int light_percent,
                                  int soil_moisture_percent,
                                  int soil_adc_raw,
                                  bool soil_calibrated,
                                  int water_level_percent,
                                  int mq2_signal_percent,
                                  float temperature_c,
                                  float humidity_percent,
                                  bool dht11_valid,
                                  bool pump_is_on)
{
    if (s_mutex == NULL) {
        s_mutex = xSemaphoreCreateMutex();
    }
    if (s_mutex == NULL || xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }

    char time_str[24];
    if (wifi_is_time_synced()) {
        time_t now;
        struct tm ti;
        time(&now);
        localtime_r(&now, &ti);
        snprintf(time_str, sizeof(time_str), "%02d:%02d:%02d", ti.tm_hour, ti.tm_min, ti.tm_sec);
    } else {
        uint32_t sec = (uint32_t)(esp_timer_get_time() / 1000000ULL);
        snprintf(time_str, sizeof(time_str), "+%02lu:%02lu:%02lu",
                 (unsigned long)(sec / 3600),
                 (unsigned long)((sec % 3600) / 60),
                 (unsigned long)(sec % 60));
    }

    s_current.light_percent = light_percent;
    s_current.soil_moisture_percent = soil_moisture_percent;
    s_current.soil_adc_raw = soil_adc_raw;
    s_current.soil_calibrated = soil_calibrated;
    s_current.water_level_percent = water_level_percent;
    s_current.mq2_signal_percent = mq2_signal_percent;
    if (dht11_valid) {
        s_current.temperature_c = temperature_c;
        s_current.humidity_percent = humidity_percent;
    }
    s_current.dht11_valid = dht11_valid;
    s_current.pump_is_on = pump_is_on;
    snprintf(s_current.time_str, sizeof(s_current.time_str), "%s", time_str);

    int insert_idx;
    if (s_log_count < WEBSERVER_MAX_LOGS) {
        insert_idx = (s_log_start + s_log_count) % WEBSERVER_MAX_LOGS;
        s_log_count++;
    } else {
        insert_idx = s_log_start;
        s_log_start = (s_log_start + 1) % WEBSERVER_MAX_LOGS;
    }

    webserver_log_entry_t *entry = &s_logs[insert_idx];
    entry->light_percent = light_percent;
    entry->soil_moisture_percent = soil_moisture_percent;
    entry->soil_adc_raw = soil_adc_raw;
    entry->soil_calibrated = soil_calibrated;
    entry->water_level_percent = water_level_percent;
    entry->mq2_signal_percent = mq2_signal_percent;
    entry->temperature_c = temperature_c;
    entry->humidity_percent = humidity_percent;
    entry->dht11_valid = dht11_valid;
    entry->pump_is_on = pump_is_on;
    snprintf(entry->time_str, sizeof(entry->time_str), "%s", time_str);

    xSemaphoreGive(s_mutex);
}

void webserver_clear_logs(void)
{
    if (s_mutex && xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        s_log_start = 0;
        s_log_count = 0;
        xSemaphoreGive(s_mutex);
    }
}

static void on_ip_event(void *arg, esp_event_base_t event_base,
                        int32_t event_id, void *event_data)
{
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = (const ip_event_got_ip_t *)event_data;
        webserver_start();
        ESP_LOGI(TAG, "==========================================================");
        ESP_LOGI(TAG, "  WEBSERVER LOG CAM BIEN SAN SANG: http://" IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "==========================================================");
    }
}

static void on_wifi_disconnect(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        // Khong nhat thiet phai stop httpd ngay de giu connection handler sach
    }
}

esp_err_t webserver_init(void)
{
    if (s_mutex == NULL) {
        s_mutex = xSemaphoreCreateMutex();
    }

    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_ip_event, NULL);
    esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &on_wifi_disconnect, NULL);

    if (wifi_is_connected()) {
        webserver_start();
    }

    return ESP_OK;
}

webserver_pump_mode_t webserver_get_pump_mode(void)
{
    webserver_pump_mode_t m;
    if (s_mutex && xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        m = s_pump_mode;
        xSemaphoreGive(s_mutex);
    } else {
        m = s_pump_mode;
    }
    return m;
}

void webserver_set_pump_mode(webserver_pump_mode_t mode)
{
    if (s_mutex && xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        s_pump_mode = mode;
        xSemaphoreGive(s_mutex);
    } else {
        s_pump_mode = mode;
    }
}
