// Shared selective Chart.js setup for the analyzer and statistics charts.
// Importing 'chart.js/auto' registers every controller/scale/element (bar,
// pie, radar, time scale, ...), which defeats tree-shaking in a bundle that
// is embedded in device flash and served over the ESP32's Wi-Fi. These pages
// only draw line charts on linear/category scales.
import {
  Chart,
  CategoryScale,
  Filler,
  Legend,
  LinearScale,
  LineController,
  LineElement,
  PointElement,
  Tooltip,
} from 'chart.js';

Chart.register(
  LineController,
  LineElement,
  PointElement,
  LinearScale,
  CategoryScale,
  Filler,
  Legend,
  Tooltip,
);

export default Chart;
