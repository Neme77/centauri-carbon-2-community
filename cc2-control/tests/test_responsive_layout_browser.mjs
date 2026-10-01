// Optional real-browser test: see docs/PRINT_TUNING.md for setup.
import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
const webRoot = path.resolve(
	path.dirname(fileURLToPath(import.meta.url)),
	"../web",
);
import http from "node:http";
const server = http.createServer((req, res) => {
	const name = req.url.startsWith("/i18n/")
		? "locales/" + path.basename(req.url)
		: "index.html";
	res.setHeader(
		"Content-Type",
		name.endsWith(".json") ? "application/json" : "text/html",
	);
	res.end(fs.readFileSync(path.join(webRoot, name)));
});
await new Promise((resolve) => server.listen(0, "127.0.0.1", resolve));
const { chromium: playwright } = await import(
	process.env.CC2_PLAYWRIGHT_MODULE || "playwright"
);
const browser = await playwright.launch({
	headless: true,
	...(process.env.CC2_BROWSER_PATH
		? { executablePath: process.env.CC2_BROWSER_PATH }
		: {}),
	args: [
		"--no-sandbox",
		"--disable-gpu",
		"--disable-software-rasterizer",
		"--single-process",
		"--no-zygote",
	],
});
const page = await browser.newPage({ viewport: { width: 390, height: 844 } });
const errors = [];
page.on("pageerror", (e) => errors.push(e.message));
let d = {
	connected: true,
	last_message_age: 0,
	machine: { status: 2, status_name: "Printing", progress: 20 },
	print: {
		filename: "Buddha.gcode",
		uuid: "test-job",
		state: "printing",
		current_layer: 20,
		total_layers: 227,
		duration: 120,
		remaining: 1200,
	},
	extruder: { temperature: 210, target: 210 },
	heater_bed: { temperature: 60, target: 60 },
	chamber: { temperature: 28 },
	fans: { part: 153, aux: 0, box: 0 },
	hardware: { camera: false, light: 1 },
	motion: { x: 20, y: 20, z: 4, homed_axes: "xyz" },
	tuning: { speed_percent: 100, flow_percent: 100, live_velocity: 80 },
};
const longFilename =
	"ECC2_0.4_Buddha_Elegoo_PLA_very_long_print_name_with_extended_metadata_0.2_25m47s.gcode";
const commands = [];
let failPrinter = false;
await page.route("**/api/**", async (route) => {
	const req = route.request(),
		path = new URL(req.url()).pathname;
	let out = {};
	if (path === "/api/printer") {
		if (failPrinter)
			return route.fulfill({
				status: 503,
				contentType: "application/json",
				body: '{"error":"offline fixture"}',
			});
		out = d;
	}
	if (path === "/api/health")
		out = { version: "test", mqtt_connected: true, mqtt_registered: true };
	if (path === "/api/setup") out = { required: false, configured: true };
	if (path === "/api/preferences") out = { language: "en", theme: "dark" };
	if (path === "/api/orca/pending-print") out = { pending: false };
	if (path === "/api/material-presets") out = { presets: [] };
	if (path === "/api/gcode-files")
		out = {
			internal: { files: [{ path: longFilename, size: 5190855, modified: 1 }] },
			usb: { files: [] },
		};
	if (path === "/api/control") {
		const command = req.postData();
		commands.push(command);
		const [, kind, value] = command.split(":");
		if (command.startsWith("tune:"))
			setTimeout(() => {
				d.tuning[kind + "_percent"] = Number(value);
			}, 200);
		return route.fulfill({
			status: 202,
			contentType: "application/json",
			body: '{"accepted":true}',
		});
	}
	return route.fulfill({
		contentType: "application/json",
		body: JSON.stringify(out),
	});
});
await page.goto(`http://127.0.0.1:${server.address().port}`);

for (const width of [1024, 1280, 1366, 1440, 1920, 320, 360, 390, 740]) {
 await page.setViewportSize({ width, height: width === 740 ? 360 : 900 });
 for (const name of ['dashboard', 'control', 'job', 'files', 'bed', 'canvas', 'console', 'settings']) {
  if (width < 640) await page.getByRole('button', {name: 'Expand menu', exact: true}).click();
  await page.locator(`#cc2-navigation a[href="#${name}"]`).click();
  for (let toggle = 0; toggle < (width >= 768 ? 2 : 1); toggle++) {
   await page.waitForTimeout(350);
   const layout = await page.evaluate(() => {
    const main = document.querySelector('main');
    const rect = main.getBoundingClientRect();
    return { width: innerWidth, scroll: document.documentElement.scrollWidth,
      mainLeft: rect.left, sidebarRight: document.querySelector('aside').getBoundingClientRect().right,
      cards: [...main.querySelectorAll('section')].map(el => {
       const r = el.getBoundingClientRect(); return { left: r.left, right: r.right };
      }) };
   });
   assert.ok(layout.scroll <= layout.width, `${name} ${width}: ${JSON.stringify(layout)}`);
   if (width >= 768) assert.equal(layout.mainLeft, layout.sidebarRight);
   for (const card of layout.cards) {
    assert.ok(card.left >= layout.mainLeft && card.right <= width, `${name}: card outside main`);
   }
   if (name === 'control' && width === 1440) {
    const columns = await page.locator('main > div.grid').first().evaluate(el => getComputedStyle(el).gridTemplateColumns.split(' ').length);
    assert.equal(columns, toggle === 0 ? 2 : 3);
   }
   if (process.env.CC2_SCREENSHOTS && name === 'control' && width === 1366) {
    fs.mkdirSync(process.env.CC2_SCREENSHOTS, {recursive:true});
    await page.screenshot({path:path.join(process.env.CC2_SCREENSHOTS, `control-${toggle}.png`),fullPage:true});
   }
   if (width >= 768) await page.getByRole('button', {name: /^(Expand menu|Collapse menu)$/}).click();
  }
 }
}
assert.deepEqual(errors, []);
console.log('PASS all eight pages: desktop sidebar open/collapsed, portrait and landscape containment');
await browser.close();server.close();
