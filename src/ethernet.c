#include "config.h"
#include "driver/gpio.h"
#include "esp_netif.h"
#include "esp_eth.h"

// IO0 : rmii_emac_REF_CLK
// IO13: rmii_emac_rx_er
// IO21: RMII_EMAC_TX_EN
// IO19: RMII_EMAC_TXD0
// IO22: RMII_EMAC_TXD1                                                                                                                                                                      // IO27: RMII_EMAC_CRS_DV
// IO27:  MODE2 = CRS_DV/MODE2 = IO27
// IO26:  MODE1 = RXD1/MODE1 = IO26
// IO25: MODE0 = RXD0/MODE0 = IO25
// io16: CLOCK_EN

static esp_eth_handle_t eth_handle = NULL;

/* Initialize Ethernet */
void init_ethernet(void) {
    #define GPIO_PIN 16

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << GPIO_PIN), // Pin mask for GPIO16
        .mode = GPIO_MODE_OUTPUT,          // Set as output
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,    // No interrupts
    };
    gpio_config(&io_conf);
    gpio_set_level(GPIO_PIN, 1);



    esp_netif_config_t netif_config = ESP_NETIF_DEFAULT_ETH();
    esp_netif_t *eth_netif = esp_netif_new(&netif_config);

    // EMAC Configuration
    eth_esp32_emac_config_t esp32_emac_config = ETH_ESP32_EMAC_DEFAULT_CONFIG();

    // SMI GPIO Configuration
    esp32_emac_config.smi_gpio.mdc_num = 23;   // MDC pin
    esp32_emac_config.smi_gpio.mdio_num = 18;  // MDIO pin

    // Clock Configuration
    esp32_emac_config.clock_config.rmii.clock_mode = EMAC_CLK_EXT_IN; // External 50 MHz clock
    esp32_emac_config.clock_config.rmii.clock_gpio = 0; // GPIO0 for external clock

    // MAC and PHY Configurations
    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();

    // PHY Settings
    phy_config.phy_addr = 1;  // Default PHY address for LAN8720
    phy_config.reset_gpio_num = -1; // No GPIO for reset, rely on hardware reset (RC filter)
    phy_config.reset_timeout_ms = 1000;
    phy_config.autonego_timeout_ms = 5000;

    // Create MAC and PHY objects
    esp_eth_mac_t *mac = esp_eth_mac_new_esp32(&esp32_emac_config, &mac_config);
    esp_eth_phy_t *phy = esp_eth_phy_new_lan87xx(&phy_config);

    // Configure Ethernet driver
    esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(mac, phy);
    ESP_ERROR_CHECK(esp_eth_driver_install(&eth_config, &eth_handle));

    // Attach Ethernet driver to network interface
    ESP_ERROR_CHECK(esp_netif_attach(eth_netif, esp_eth_new_netif_glue(eth_handle)));

    // Start Ethernet driver
    ESP_ERROR_CHECK(esp_eth_start(eth_handle));
}
