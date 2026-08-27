export const saveToStorage = (key, value, storage = localStorage) => {
  try {
    storage.setItem(key, JSON.stringify(value));
  } catch (e) {
    console.error(`Failed to save "${key}" to localStorage:`, e);
  }
};

export const loadFromStorage = (key, defaultValue = null, storage = localStorage) => {
  try {
    const item = storage.getItem(key);
    return item ? JSON.parse(item) : defaultValue;
  } catch (e) {
    console.error(`Failed to load "${key}" from localStorage:`, e);
    return defaultValue;
  }
};

// The localStorage-backed shot library (getSortedLibrary/saveToLibrary/...)
// was removed: IndexedDBService replaced it, nothing imported it, and storing
// full sample arrays in localStorage would blow the ~5 MB quota within a
// handful of shots.
