const state = {
  restaurants: [],
  menu: [],
  selectedRestaurant: "ALL",
  selectedCategory: "ALL",
  menuLoadError: false,
  cart: { lines: [], total: 0, lineCount: 0, undoStack: [] },
  ordersState: { queueSize: 0, queue: [], allOrders: [] },
  selectedOrderId: null,
};

const STAGES = ["Order Received", "Preparing", "Out for Delivery", "Delivered"];
const DISH_ICONS = ["🥗", "🍜", "🍛", "🥙", "🍕", "🍰", "🥪", "🍲"];
const BADGE_CLASSES = ["", "badge-green", "badge-yellow"];

function escapeHtml(value) {
  return String(value)
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;")
    .replace(/'/g, "&#39;");
}

function formatPrice(value) {
  return `₹${Number(value || 0).toFixed(2)}`;
}

let toastTimer = null;
function showToast(message, isError = false) {
  const toast = document.getElementById("toast");
  toast.textContent = message;
  toast.className = `toast show${isError ? " error" : ""}`;
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => toast.classList.remove("show"), 2800);
}

async function api(url, options = {}) {
  const response = await fetch(url, {
    headers: { "Content-Type": "application/json" },
    ...options,
  });

  let data;
  try {
    data = await response.json();
  } catch {
    throw new Error("The request could not be completed.");
  }

  if (!response.ok) {
    throw new Error(data.error || "The request could not be completed.");
  }
  return data;
}

function displayError(message) {
  const text = String(message || "");
  if (/not found|unknown|does not exist/i.test(text)) {
    return "We couldn't find that. Check the details and try again.";
  }
  return "Something went wrong. Please try again.";
}

async function init() {
  const [restaurantsResult, menuResult, cartResult, ordersResult] = await Promise.allSettled([
    api("/api/restaurants"),
    api("/api/menu"),
    api("/api/cart"),
    api("/api/orders"),
  ]);

  if (restaurantsResult.status === "fulfilled" && Array.isArray(restaurantsResult.value.restaurants)) {
    state.restaurants = restaurantsResult.value.restaurants;
  }

  if (menuResult.status === "fulfilled" && Array.isArray(menuResult.value.items)) {
    state.menu = menuResult.value.items;
    state.menuLoadError = false;
  } else {
    state.menu = [];
    state.menuLoadError = true;
  }

  if (cartResult.status === "fulfilled") {
    state.cart = cartResult.value;
  }

  if (ordersResult.status === "fulfilled") {
    state.ordersState = ordersResult.value;
    if (ordersResult.value.queue.length > 0) {
      state.selectedOrderId = ordersResult.value.queue[0].id;
    } else if (ordersResult.value.allOrders.length > 0) {
      state.selectedOrderId = ordersResult.value.allOrders[ordersResult.value.allOrders.length - 1].id;
    }
  }

  renderRestaurants();
  renderMenu();
  renderCart();
  renderOrders();

  if (state.menuLoadError) {
    showToast("Menu unavailable. Start ./food_server, then open http://localhost:8080.", true);
  } else if (restaurantsResult.status === "rejected") {
    showToast("Restaurant filters couldn't load. Please refresh to try again.", true);
  } else if (cartResult.status === "rejected" || ordersResult.status === "rejected") {
    showToast("Some ordering features couldn't load. Please refresh to try again.", true);
  }
}

function renderRestaurants() {
  const container = document.getElementById("restaurantFilters");
  const categories = [...new Set(state.menu.map((item) => item.category).filter(Boolean))].sort();
  if (state.selectedCategory !== "ALL" && !categories.includes(state.selectedCategory)) {
    state.selectedCategory = "ALL";
  }

  const restaurantButtons = [
    `<button class="segment-btn ${state.selectedRestaurant === "ALL" ? "active" : ""}" type="button" data-restaurant="ALL">All</button>`,
    ...state.restaurants.map(
      (restaurant) => `<button class="segment-btn ${state.selectedRestaurant === restaurant.id ? "active" : ""}" type="button" data-restaurant="${escapeHtml(restaurant.id)}">${escapeHtml(restaurant.name)}</button>`,
    ),
  ];
  const categoryButtons = [
    `<button class="segment-btn ${state.selectedCategory === "ALL" ? "active" : ""}" type="button" data-category="ALL">All</button>`,
    ...categories.map(
      (category) => `<button class="segment-btn ${state.selectedCategory === category ? "active" : ""}" type="button" data-category="${escapeHtml(category)}">${escapeHtml(category)}</button>`,
    ),
  ];
  container.innerHTML = `
    <div class="filter-group">
      <span class="filter-label">Restaurants</span>
      <div class="filter-options">${restaurantButtons.join("")}</div>
    </div>
    <div class="filter-group">
      <span class="filter-label">Categories</span>
      <div class="filter-options">${categoryButtons.join("")}</div>
    </div>`;

  container.querySelectorAll("[data-restaurant]").forEach((button) => {
    button.addEventListener("click", () => {
      state.selectedRestaurant = button.dataset.restaurant;
      renderRestaurants();
      renderMenu();
    });
  });
  container.querySelectorAll("[data-category]").forEach((button) => {
    button.addEventListener("click", () => {
      state.selectedCategory = button.dataset.category;
      renderRestaurants();
      renderMenu();
    });
  });
}

function renderMenu() {
  const query = document.getElementById("searchInput").value.trim().toLowerCase();
  const list = document.getElementById("menuList");
  if (state.menuLoadError) {
    list.innerHTML = `
      <div class="empty-menu menu-load-error">
        <span aria-hidden="true">🍽️</span>
        <p>The menu couldn't load.</p>
        <p>Start <code>./food_server</code>, then open <strong>http://localhost:8080</strong>.</p>
      </div>`;
    return;
  }

  const filtered = state.menu.filter((item) => {
    const matchesRestaurant =
      state.selectedRestaurant === "ALL" || item.restaurantId === state.selectedRestaurant;
    const matchesCategory =
      state.selectedCategory === "ALL" || item.category === state.selectedCategory;
    const matchesSearch =
      !query ||
      String(item.id).toLowerCase().includes(query) ||
      String(item.name).toLowerCase().includes(query) ||
      String(item.restaurantName).toLowerCase().includes(query) ||
      String(item.category).toLowerCase().includes(query);
    return matchesRestaurant && matchesCategory && matchesSearch;
  });

  if (filtered.length === 0) {
    const hasFilters = query || state.selectedRestaurant !== "ALL" || state.selectedCategory !== "ALL";
    list.innerHTML = `
      <div class="empty-menu">
        <span aria-hidden="true">🍽️</span>
        <p>${hasFilters ? "No dishes match those filters. Try another selection." : "No dishes are available right now."}</p>
      </div>`;
    return;
  }

  list.innerHTML = filtered
    .map((item, index) => {
      const icon = DISH_ICONS[index % DISH_ICONS.length];
      const badgeClass = BADGE_CLASSES[index % BADGE_CLASSES.length];
      return `
        <article class="dish-row">
          <div class="dish-main">
            <span class="dish-badge ${badgeClass}" aria-hidden="true">${icon}</span>
            <div class="item-info">
              <span class="item-name">${escapeHtml(item.name)}</span>
              <span class="item-sub">${escapeHtml(item.restaurantName || "Made fresh")} · ${escapeHtml(item.category || "Menu favorite")}</span>
              <span class="item-id">${escapeHtml(item.id)}</span>
            </div>
          </div>
          <span class="item-price">${formatPrice(item.price)}</span>
          <button class="btn btn-add" type="button" data-add="${escapeHtml(item.id)}" aria-label="Add ${escapeHtml(item.name)} to your bag">Add</button>
        </article>`;
    })
    .join("");

  list.querySelectorAll("[data-add]").forEach((button) => {
    button.addEventListener("click", () => addToCart(button.dataset.add));
  });
}

function renderCart() {
  const list = document.getElementById("cartList");
  const totalUnits = state.cart.lines.reduce((total, line) => total + line.qty, 0);
  document.getElementById("cartCountPill").textContent =
    `${totalUnits} ${totalUnits === 1 ? "item" : "items"}`;
  document.getElementById("cartTotal").textContent = formatPrice(state.cart.total);

  const hasItems = state.cart.lines.length > 0;
  document.getElementById("undoBtn").disabled = !state.cart.undoStack || state.cart.undoStack.length === 0;
  document.getElementById("clearBtn").disabled = !hasItems;
  document.getElementById("checkoutBtn").disabled = !hasItems;

  if (!hasItems) {
    list.innerHTML = `
      <div class="empty-menu">
        <span aria-hidden="true">🛍️</span>
        <p>Your bag is waiting for something delicious.</p>
      </div>`;
    return;
  }

  list.innerHTML = state.cart.lines
    .map(
      (line) => `
        <div class="bag-row">
          <div class="item-info">
            <span class="item-name">${escapeHtml(line.name)}</span>
            <span class="item-sub">${formatPrice(line.price)} each · ${formatPrice(line.subtotal)}</span>
          </div>
          <div class="stepper-ctrl" aria-label="${escapeHtml(line.name)} quantity">
            <button class="step-icon" type="button" data-dec="${escapeHtml(line.id)}" data-qty="${line.qty}" aria-label="Decrease quantity">−</button>
            <span class="qty-num">${line.qty}</span>
            <button class="step-icon" type="button" data-inc="${escapeHtml(line.id)}" data-qty="${line.qty}" aria-label="Increase quantity">+</button>
            <button class="step-icon remove" type="button" data-del="${escapeHtml(line.id)}" aria-label="Remove ${escapeHtml(line.name)}">×</button>
          </div>
        </div>`,
    )
    .join("");

  list.querySelectorAll("[data-inc]").forEach((button) =>
    button.addEventListener("click", () =>
      updateCartQty(button.dataset.inc, Number(button.dataset.qty) + 1),
    ),
  );
  list.querySelectorAll("[data-dec]").forEach((button) =>
    button.addEventListener("click", () =>
      updateCartQty(button.dataset.dec, Number(button.dataset.qty) - 1),
    ),
  );
  list.querySelectorAll("[data-del]").forEach((button) =>
    button.addEventListener("click", () => removeCartItem(button.dataset.del)),
  );
}

function getOrderStage(order) {
  if (Number.isInteger(Number(order.statusIndex))) {
    return Math.max(0, Math.min(STAGES.length - 1, Number(order.statusIndex)));
  }
  const status = String(order.status || "").toLowerCase();
  if (/complete|deliver/.test(status)) return 3;
  if (/ready|out for delivery/.test(status)) return 2;
  if (/prepar/.test(status)) return 1;
  return 0;
}

function renderOrders() {
  const card = document.getElementById("activeOrderCard");
  const advanceButton = document.getElementById("advanceBtn");
  const queueChips = document.getElementById("queueChips");
  const queueCount = document.getElementById("queueCountPill");
  const { queue, allOrders } = state.ordersState;
  const activeOrders = queue || [];

  queueCount.textContent = `${activeOrders.length} active`;
  advanceButton.disabled = activeOrders.length === 0;

  let order = (allOrders || []).find((entry) => entry.id === state.selectedOrderId);
  if (!order && activeOrders.length > 0) {
    order = activeOrders[0];
    state.selectedOrderId = order.id;
  }

  if (!order) {
    card.innerHTML = `
      <div class="empty-order">
        <span class="empty-illustration" aria-hidden="true">🛵</span>
        <p>Your next favorite meal is just an order away.</p>
      </div>`;
  } else {
    const currentStage = getOrderStage(order);
    const timeline = STAGES.map((label, index) => {
      const className = index < currentStage ? "done" : index === currentStage ? "current" : "";
      return `<li class="timeline-step ${className}"><span class="timeline-dot" aria-hidden="true"></span><span>${label}</span></li>`;
    }).join("");
    const summary = (order.lines || [])
      .map((line) => `${escapeHtml(line.name)} ×${line.qty}`)
      .join(" · ");

    card.innerHTML = `
      <div class="order-title">
        <h3>Your order #${escapeHtml(order.id)}</h3>
        <span>${formatPrice(order.total)}</span>
      </div>
      <ol class="timeline">${timeline}</ol>
      ${summary ? `<div class="order-items-note">${summary}</div>` : ""}`;
  }

  if (activeOrders.length === 0) {
    queueChips.innerHTML = `<span class="no-active-orders">No active orders right now</span>`;
    return;
  }

  queueChips.innerHTML = activeOrders
    .map(
      (entry) => `
        <button class="queue-chip ${entry.id === state.selectedOrderId ? "active" : ""}" type="button" data-oid="${escapeHtml(entry.id)}">
          Order #${escapeHtml(entry.id)}
        </button>`,
    )
    .join("");

  queueChips.querySelectorAll("[data-oid]").forEach((button) =>
    button.addEventListener("click", () => {
      state.selectedOrderId = Number(button.dataset.oid);
      renderOrders();
    }),
  );
}

async function addToCart(itemId) {
  try {
    state.cart = await api("/api/cart/items", {
      method: "POST",
      body: JSON.stringify({ itemId, qty: 1 }),
    });
    renderCart();
    showToast("Added to your bag.");
  } catch (error) {
    showToast(displayError(error.message), true);
  }
}

async function updateCartQty(itemId, qty) {
  try {
    state.cart = await api(`/api/cart/items/${encodeURIComponent(itemId)}`, {
      method: "PUT",
      body: JSON.stringify({ qty }),
    });
    renderCart();
  } catch (error) {
    showToast(displayError(error.message), true);
  }
}

async function removeCartItem(itemId) {
  try {
    state.cart = await api(`/api/cart/items/${encodeURIComponent(itemId)}`, {
      method: "DELETE",
    });
    renderCart();
    showToast("Removed from your bag.");
  } catch (error) {
    showToast(displayError(error.message), true);
  }
}

async function handleUndo() {
  try {
    const response = await api("/api/cart/undo", { method: "POST" });
    state.cart = response.cart;
    renderCart();
    showToast("Your last bag change has been undone.");
  } catch (error) {
    showToast(displayError(error.message), true);
  }
}

async function handleClearCart() {
  try {
    state.cart = await api("/api/cart/clear", { method: "POST" });
    renderCart();
    showToast("Your bag is now clear.");
  } catch (error) {
    showToast(displayError(error.message), true);
  }
}

async function handleCheckout() {
  try {
    const response = await api("/api/orders", { method: "POST" });
    state.cart = response.cart;
    state.ordersState = response.ordersState;
    state.selectedOrderId = response.order.id;
    renderCart();
    renderOrders();
    showToast(`Order #${response.order.id} received. Thanks for choosing Savor!`);
  } catch (error) {
    showToast(displayError(error.message), true);
  }
}

async function handleAdvanceOrder() {
  try {
    const response = await api("/api/orders/advance", { method: "POST" });
    state.ordersState = response.ordersState;
    state.selectedOrderId = response.advancedOrderId;
    renderOrders();
    const order = state.ordersState.allOrders.find((entry) => entry.id === response.advancedOrderId);
    const stage = order ? STAGES[getOrderStage(order)] : "updated";
    showToast(`Order #${response.advancedOrderId}: ${stage}.`);
  } catch (error) {
    showToast(displayError(error.message), true);
  }
}

async function handleSearch() {
  const query = document.getElementById("searchInput").value.trim();
  const banner = document.getElementById("lookupBanner");
  renderMenu();
  banner.classList.add("hidden");

  if (!query) {
    showToast("Enter a dish, restaurant, or item code to search.", true);
    return;
  }

  const normalizedQuery = query.toLowerCase();
  const matchedItem = state.menu.find((item) => String(item.id).toLowerCase() === normalizedQuery);
  const matchedRestaurant = state.restaurants.find(
    (restaurant) => String(restaurant.id).toLowerCase() === normalizedQuery,
  );

  try {
    if (matchedItem) {
      const item = await api(`/api/menu/${encodeURIComponent(query)}`);
      banner.textContent = `${item.name} · ${item.restaurantName} · ${formatPrice(item.price)}`;
      banner.classList.remove("hidden");
      return;
    }
    if (matchedRestaurant) {
      const restaurant = await api(`/api/restaurants/${encodeURIComponent(query)}`);
      banner.textContent = `${restaurant.name} · ${restaurant.tagline || "Explore the menu"}`;
      banner.classList.remove("hidden");
      return;
    }
    if (!state.menu.some((item) => (
      String(item.name).toLowerCase().includes(normalizedQuery) ||
      String(item.restaurantName).toLowerCase().includes(normalizedQuery)
    ))) {
      showToast("No dishes or restaurants matched your search.");
    }
  } catch (error) {
    showToast(displayError(error.message), true);
  }
}

async function handleOrderLookup() {
  const input = document.getElementById("orderLookupInput");
  const id = input.value.trim();
  if (!id || Number(id) <= 0) {
    showToast("Enter a valid order number.", true);
    return;
  }

  try {
    const response = await api(`/api/orders/${encodeURIComponent(id)}`);
    state.selectedOrderId = response.order.id;
    if (!(state.ordersState.allOrders || []).some((order) => order.id === response.order.id)) {
      state.ordersState.allOrders = [...(state.ordersState.allOrders || []), response.order];
    } else {
      state.ordersState.allOrders = state.ordersState.allOrders.map((order) =>
        order.id === response.order.id ? response.order : order,
      );
    }
    renderOrders();
    showToast(`You're viewing order #${response.order.id}.`);
  } catch (error) {
    showToast(displayError(error.message), true);
  }
}

document.getElementById("searchForm").addEventListener("submit", (event) => {
  event.preventDefault();
  handleSearch();
});
document.getElementById("searchInput").addEventListener("input", () => {
  document.getElementById("lookupBanner").classList.add("hidden");
  renderMenu();
});
document.getElementById("orderLookupForm").addEventListener("submit", (event) => {
  event.preventDefault();
  handleOrderLookup();
});
document.getElementById("undoBtn").addEventListener("click", handleUndo);
document.getElementById("clearBtn").addEventListener("click", handleClearCart);
document.getElementById("checkoutBtn").addEventListener("click", handleCheckout);
document.getElementById("advanceBtn").addEventListener("click", handleAdvanceOrder);

init();
