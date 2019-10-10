<?php

/**
 * @file
 * The administration forms, and their Validate and Submit functions.
 */

/**
 * Payment method callback.
 */
function uc_gc_client_uc_payment_method_callback($op, &$order, $form = NULL, &$form_state = NULL) {
  switch ($op) {
    case 'settings':
      if (isset($form_state['values']['uc_gc_client_sandbox'])) {
        $sandbox = $form_state['values']['uc_gc_client_sandbox'];
      }
      else {
        $sandbox = variable_get('uc_gc_client_sandbox', TRUE);
      }
      $sandbox ? $ext = '_sandbox' : $ext = '_live';
      $auth = uc_gc_client_authenticate();
      if ($auth == 200) {
        $response = uc_gc_client_get();
        isset($response->environ) ? $connected = TRUE : $connected = FALSE;
      }
      else {
        $connected = FALSE;
      }

      $form = array(
        '#prefix' => '<div id="connect-wrapper">',
        '#suffix' => '</div>',
      );

      $form['connect'] = array(
        '#type' => 'fieldset',
        '#title' => t('Connect with GoCardless'),
        '#collapsible' => FALSE,
        '#collapsed' => FALSE,
      );
      $form['connect']['uc_gc_client_sandbox'] = array(
        '#type' => 'checkbox',
        '#title' => '<b>' .  t('Enable Sandbox') . '</b>',
        '#description' => t('Sandbox: GoCardless will operate in a test environment, and no real banking activity will occur.'),
        '#default_value' => $sandbox,
        '#ajax' => array(
          'callback' => 'uc_gc_client_sandbox_callback',
          'wrapper' => 'connect-wrapper',
          'method' => 'replace',
        ),
      );

      $markup = '<br /><p><b>' . t('Connect / disconnect with GoCardless') . '</b></p>';
      if (!$connected) {
        $markup .= '<p>' . t('After clicking \'Connect\' you will be redirected to the GoCardless where you can create an account and connect your site as a client of Seamless-CMS.co.uk') . '</p>';
        $form['connect']['markup'] = array(
          '#markup' => $markup,
        );
        $form['connect']['connect'] = array(
          '#type' => 'submit',
          '#disabled' => !$sandbox && !isset($_SERVER['HTTPS']) ? TRUE : FALSE,
          '#value' => $sandbox ? t('Connect SANDBOX') : t('Connect LIVE'),
          '#submit' => array('uc_gc_client_connect_submit'),
          '#suffix' => !$sandbox && !isset($_SERVER['HTTPS']) ? '<br /><i>' . t('Site needs to be secure (https) before you can connect to GoCardless LIVE.') . '</i>' : NULL,
        );
      }
      else {
        $form['connect']['markup'] = array(
          '#markup' => $markup,
        );
        $form['connect']['ext'] = array(
          '#type' => 'value',
          '#value' => $ext,
        );
        $form['connect']['disconnect'] = array(
          '#type' => 'submit',
          '#value' => $sandbox ? t('Disconnect SANDBOX') : t('Disconnect LIVE'),
          '#submit' => array('uc_gc_client_disconnect_submit'),
          '#attributes' => array('onclick' => 'if (!confirm("' . t('Are you sure you want to disconnect your site from GoCardless?') . '")) {return false;}'),
        );
      }

      global $base_url;
      $webhook_url = $base_url . '/gc_client/webhook';
      $gc_webhook_url_env = $sandbox ? '-sandbox' : '';
      $gc_webhook_url = 'https://manage' . $gc_webhook_url_env . '.gocardless.com/developers/webhook-endpoints/create';
      $webhook_prefix = t('To receive webhooks, add <i>@webhook_url</i> as webhook URL and set the secret to the same as the Webhook Secret field from <a target="new" href="@gc_webhook_url">here</a>.', array(
        '@webhook_url' => $webhook_url,
        '@gc_webhook_url' => $gc_webhook_url,
      )) . '<br /><br />';

      $form['connect']['uc_gc_client_webhook' . $ext] = array(
        '#type' => 'textfield',
        '#title' => t('Webhook secret'),
        '#default_value' => $sandbox ? variable_get('uc_gc_client_webhook_sandbox') : variable_get('uc_gc_client_webhook_live'),
        '#field_prefix' => $webhook_prefix,
      );

      // Global.
      $form['global'] = array(
        '#type' => 'fieldset',
        '#title' => t('Global settings'),
        '#collapsible' => FALSE,
        '#collapsed' => FALSE,
      );
      $form['global']['uc_gc_client_dom'] = array(
        '#type' => 'textfield',
        '#title' => t('Day(s) of month that first payments go out on'),
        '#default_value' => variable_get('uc_gc_client_dom'),
        '#size' => 25,
        '#maxlength' => 24,
        '#description' => t('Enter one or more days of the month, upon which direct debits will start. The system will automatically choose the next available date from those you have provided. Values must be seperated by a comma.<br />These values will be ignored if there is a valid Start Date set for individual products, or if they are set to create a payment immediately.'),
        '#required' => FALSE,
      );
      $form['global']['uc_gc_client_payments_tab'] = array(
        '#type' => 'checkbox',
        '#title' => t('Hide Payments tab'),
        '#default_value' => variable_get('uc_gc_client_payments_tab'),
        '#description' => t('Ubercart Payments do not work with GoCardless, so checking this will hide the Payments tab when viewing any orders created through GoCardless.'),
      );
      $form['global']['uc_gc_client_currencies'] = array(
        '#type' => 'checkbox',
        '#title' => t('Create payments in foreign currencies'),
        '#default_value' => variable_get('uc_gc_client_currencies'),
        '#description' => t("Use foreign currency, and adjust payment amount for international customers, according to current exchange rates at <a target='new' href='http://fixer.io'>fixer.io</a>. This only applies when the currency of the customer's country is different to the store's default currency. SEPA and /or AutoGiro regions need to be enabled on your GoCardless account for this to work."),
      );
      $form['global']['uc_gc_client_fixer'] = array(
        '#type' => 'textfield',
        '#title' => t('fixer.io'),
        '#default_value' => variable_get('uc_gc_client_fixer', NULL),
        '#description' => t('API key for fixer.io'),
        '#size' => 40,
        '#maxlength' => 40,
        '#states' => array(
          'visible' => array(
            'input[name="uc_gc_client_currencies"]' => array('checked' => TRUE),
          ),
        ),
      );

      // Payments.
      $form['payments'] = array(
        '#type' => 'fieldset',
        '#title' => t('One-off Payments settings'),
        '#collapsible' => FALSE,
        '#collapsed' => FALSE,
      );
      $form['payments']['uc_gc_client_payment_limit'] = array(
        '#type' => 'textfield',
        '#title' => t('Maximum payments'),
        '#default_value' => variable_get('uc_gc_client_payment_limit', 3),
        '#size' => 3,
        '#maxlength' => 3,
        '#description' => t("The maximum number of payments that can be raised automatically, per order, per day. If the amount is exceeded, a warning email is sent to the specified address above. Leave unset for unlimitted."),
        '#element_validate' => array('element_validate_integer_positive'),
        '#required' => FALSE,
      );
      $form['payments']['uc_gc_client_preauth_adjs'] = array(
        '#type' => 'textarea',
        '#title' => t('Adjustment titles'),
        '#default_value' => variable_get('uc_gc_client_preauth_adjs'),
        '#description' => t('Provide a list of pre-defined Scheduled Adjustment titles. One per line.'),
        '#resizable' => FALSE,
        '#cols' => 20,
        '#rows' => 10,
        '#prefix' => '<div style="width:400px;">',
        '#suffix' => '</div>',
      );

      // Checkout options.
      $form['checkout'] = array(
        '#type' => 'fieldset',
        '#title' => t('Checkout settings'),
        '#collapsible' => FALSE,
        '#collapsed' => FALSE,
      );
      $form['checkout']['uc_gc_client_optional_checkout_review'] = array(
        '#title' => '<b>' .  t('Optional Checkout Review page') . '</b>',
        '#type' => 'checkbox',
        '#default_value' => variable_get('uc_gc_client_optional_checkout_review'),
        '#required' => FALSE,
        '#description' => t('Check this to emit the Checkout Review page.') . '<br />' . t('Don\'t use this if you have the Ubercart Optional Checkout Review module installed, or you are using other payment methods in addition to GoCardless Client.'),
      );
      $form['checkout']['uc_gc_client_checkout_label'] = array(
        '#type' => 'textfield',
        '#title' => t('Checkout button label'),
        '#description' => t('Customize the label of the final checkout button when the customer is about to pay.'),
        '#default_value' => variable_get('uc_gc_client_checkout_label'),
      );

      // Debugging options.
      $form['debug'] = array(
        '#type' => 'fieldset',
        '#title' => t('Debugging'),
        '#collapsible' => FALSE,
        '#collapsed' => FALSE,
      );
      $form['debug']['uc_gc_client_warnings_email'] = array(
        '#type' => 'textfield',
        '#title' => t('Email'),
        '#default_value' => variable_get('uc_gc_client_warnings_email'),
        '#description' => t('Email address to send warnings.'),
        '#size' => 40,
        '#maxlength' => 40,
      );
      $form['debug']['uc_gc_client_debug_webhook'] = array(
        '#prefix' => '<p>',
        '#suffix' => '</p>',
        '#type' => 'checkbox',
        '#title' => '<b>' . t('Enable webhook debugging') . '</b>',
        '#description' => t('Webhooks recieved from GoCardless will be written to the log.'),
        '#default_value' => variable_get('uc_gc_client_debug_webhook', FALSE),
      );
      $form['debug']['uc_gc_client_debug_api'] = array(
        '#prefix' => '<p>',
        '#suffix' => '</p>',
        '#type' => 'checkbox',
        '#title' => '<b>' . t('Enable API debugging') . '</b>',
        '#description' => t('Responses from the Partner site to API posts will be written to the log.'),
        '#default_value' => variable_get('uc_gc_client_debug_api', FALSE),
      );

      // Set message if returning from Partner site after connecting.
      if (isset($_GET['status'])) {
        if ($_GET['status'] == 'connected') {
          drupal_set_message(t('You have connected successfully with GoCardless'));
        }
        //$_GET['status'] will be "failed" if the wrong password was provided
        //when receiving the authentication credentials from the partner site
        //during the connection process. See uc_gc_client_connect().
        elseif ($_GET['status'] == 'failed') {
          drupal_set_message(t('Connection with GoCardless has failed'), 'error');
        }
      }
      else {
        drupal_set_message(t('Connection with GoCardless has failed'), 'error');
      }

      $form['#submit'][] = 'uc_gc_client_admin_validate';
      return $form;
  }
}

/**
 * AJAX callback function for gc_client_uc_payment_method_callback().
 */
function uc_gc_client_sandbox_callback(&$form, &$form_state) {
  variable_set('uc_gc_client_sandbox', $form_state['values']['uc_gc_client_sandbox']);
  ctools_include('ajax');
  ctools_add_js('ajax-responder');
  $commands[] = ctools_ajax_command_reload();
  print ajax_render($commands);
  drupal_exit();
}

/**
 * Validates the gc_client payment method settings form.
 */
function uc_gc_client_admin_validate(&$form, &$form_state) {
  // Validate and process Days of Month.
  if (isset($form['uc_gc_client_dom']['#value'])) {
    // Only allow positive integers, spaces and commas.
    $doms = $form['gc_client_dom']['#value'];
    $regex = "/^[0-9, ]+$/";
    if (!preg_match($regex, $doms)) {
      form_set_error('uc_gc_client_dom', t('Only positve integers and commas please'));
    }
    // Remove spaces.
    $doms = str_replace(' ', '', $doms);
    // Remove possibilty of extra comma at end.
    $doms = explode(', ', $doms);
    foreach ($doms as $dom => $value) {
      if (empty($value)) {
        unset($doms[$dom]);
      }
      if ($value > 28) {
        form_set_error('uc_gc_client_dom', t('Number must not be greater than 28'));
      }
    }
    // Sort ascending.
    sort($doms);
    $doms = implode(', ', $doms);
    // Save processed value.
    $form_state['values']['uc_gc_client_dom'] = $doms;
    variable_set('uc_gc_client_dom', $doms);
  }

  if (!valid_email_address($form_state['values']['uc_gc_client_warnings_email'])) {
    form_set_error('uc_gc_client_warnings_email', t('Please provide a valid email address.'));
  }
}

/**
 * Implements hook_form_submit().
 *
 * Redirect to Partner site to activate the GoCardless OAuth Flow.
 */
function uc_gc_client_connect_submit($form, $form_state) {
  $form_state['values']['uc_gc_client_sandbox'] == 1 ? $env = 'sandbox' : $env = 'live';
  global $base_url;
  $client_url = urlencode($base_url . '/admin/store/settings/payment/method/gc_client');
  $url = variable_get('uc_gc_client_partner_url') . '/gc_partner/connect/' . $env;
  $token = user_password(24);
  variable_set('uc_gc_client_user_pass_' . $env, $token);
  drupal_goto($url, array(
    'query' => array(
      'mail' => variable_get('site_mail', ''),
      'client_url' => $client_url,
      'token' => $token,
      'module' => 'Ubercart 7.x',
    ),
  ));
}

/**
 * Implements hook_drupal_goto_alter().
 *
 * Closes the overlay when redirecting to an external url
 * Required when redirecting to Partner site at beginning of OAuth flow.
 */
function uc_gc_client_drupal_goto_alter(&$path, &$options, &$http_response_code) {
  if (module_exists('overlay') && overlay_get_mode() == 'child') {
    if (url_is_external($path)) {
      overlay_close_dialog($path, $options);
      overlay_deliver_empty_page();
    }
  }
}

/**
 * Implements hook_form_FORM_ID_alter().
 *
 * Add extra GoCardless settings to Product node Edit form.
 */
function uc_gc_client_form_node_form_alter(&$form, $form_state, $form_id) {
  if (uc_product_is_product_form($form)) {
    $nid = $form['nid']['#value'];
    $defaults = db_select('uc_gc_client_products', 'p')
      ->fields('p')
      ->condition('nid', $nid, '=')
      ->execute()
      ->fetchAssoc();

    $form['gc'] = array(
      '#type' => 'fieldset',
      '#title' => t('GoCardless Settings'),
      '#collapsible' => TRUE,
      '#collapsed' => TRUE,
      '#weight' => -12,
      '#attributes' => array('class' => array('gc-field')),
      '#group' => 'additional_settings',
      '#attached' => array(
        'js' => array(
          'vertical-tabs' => drupal_get_path('module', 'uc_gc_client') . '/uc_gc_client.js',
        ),
      ),
    );
    $options = array('S' => t('Subscription'), 'P' => t('One-off payments'));
    $form['gc']['gc_type'] = array(
      '#type' => 'select',
      '#title' => t('Type'),
      '#default_value' => isset($defaults['type']) ? $defaults['type'] : NULL,
      '#options' => $options,
      "#empty_option" => t('- None -'),
      '#description' => t('Select the type that will be used for new GoCardless orders.<br />If none is selected then the default type will be used.'),
      '#weight' => 0,
    );
    $form['gc']['interval_length'] = array(
      '#type' => 'textfield',
      '#title' => t('Interval length between payments'),
      '#default_value' => isset($defaults['interval_length']) ? $defaults['interval_length'] : NULL,
      '#weight' => 1,
      '#size' => 3,
      '#maxlength' => 3,
      '#description' => t('The default interval length between direct debit payments. This setting is required for Subscription payments.<br />It can be set for One-off payments, or leave unset if regular payments are not expected.<br />This value will be overridden if the Interval Attribute is set for a product.'),
      '#element_validate' => array('element_validate_integer_positive'),
      '#required' => FALSE,
    );
    $form['gc']['interval_unit'] = array(
      '#type' => 'select',
      '#title' => t('Interval unit between payments'),
      '#default_value' => isset($defaults['interval_unit']) ? $defaults['interval_unit'] : NULL,
      '#weight' => 2,
      '#options' => array(
        'week' => t('week'),
        'month' => t('month'),
        'year' => t('year'),
      ),
      "#empty_option" => t('- None -'),
      '#description' => t('The default interval unit between direct debit payments. This setting is required for Subscription payments.<br />It can be set for One-off payments, or leave unset if regular payments are not expected.<br />This value will be overridden if the Interval attribute is set for a product.'),
      '#required' => FALSE,
    );
    $form['gc']['price_x'] = array(
      '#type' => 'textfield',
      '#size' => 6,
      '#title' => t('Price multiplier'),
      '#default_value' => isset($defaults['price_x']) ? (float) $defaults['price_x'] : 1,
      '#description' => t('Set a multiplier for subscriptions and one-off payments. Use in examples where the GoCardless amount is different to the price, for example a monthly subscription for a weekly order. Leave as 1 if the GoCardless amount and the price are the same.'),
      '#weight' => 3,
      '#element_validate' => array('uc_gc_client_element_validate_positive_number'),
    );
    $form['gc']['raise_payment'] = array(
      '#type' => 'checkbox',
      '#prefix' => t('<b>Raise payment immediately</b>'),
      '#weight' => 4,
      '#default_value' => !is_null($defaults['raise_payment']) ? $defaults['raise_payment'] : 0,
      '#title' => t("Automatically raise a payment upon completion of checkout"),
      '#description' => t('Checking this will override any other settings for raising the initial payment.<br />Leave unchecked if you want another module to handle raising the initial payment.') . '<br /><br />',
    );
    $form['gc']['start_date'] = array(
      '#title' => t('Start date'),
      '#type' => module_exists('date_popup') ? 'date_popup' : 'date_select',
      '#date_format' => module_exists('date_popup') ? variable_get('date_format_uc_store') : 'd M Y',
      '#datepicker_options' => array('minDate' => 0),
      '#date_year_range' => '0:+1',
      '#default_value' => !is_null($defaults['start_date']) ? date('Y-m-d', $defaults['start_date']) : NULL,
      '#weight' => 5,
      '#description' => t('Specify a start date for the customer\'s new mandate. If this is enabled then the start date provided will override any other start date settings.'),
      '#states' => array(
        'visible' => array(
          ':input[name="raise_payment"]' => array('checked' => FALSE),
        ),
      ),
    );
    $form['#validate'][] = 'uc_gc_client_node_form_validate';
    $form['#submit'][] = 'uc_gc_client_node_form_submit';
  }
}

/**
 * Implements hook_node_insert().
 *
 * Add extra GoCardless settings to {uc_gc_client_products} table.
 */
function uc_gc_client_node_insert($node) {
  if (uc_product_is_product($node)) {
    db_insert('uc_gc_client_products')
      ->fields(array(
        'nid' => $node->nid,
        'type' => !empty($node->gc_type) ? $node->gc_type : NULL,
        'price_x' => $node->price_x,
        'start_date' => !empty($node->start_date) ? $node->start_date : NULL,
        'interval_length' => !empty($node->interval_length) ? $node->interval_length : NULL,
        'interval_unit' => !empty($node->interval_unit) ? $node->interval_unit : NULL,
      ))
      ->execute();
  }
}

/**
 * Implements hook_node_form_validate().
 */
function uc_gc_client_node_form_validate($form, &$form_state) {
  if (!empty($form_state['values']['interval_length']) && empty($form_state['values']['interval_unit'])) {
    form_set_error('interval_unit', t('Interval unit must be set if interval length is set.'));
  }
  if (!empty($form_state['values']['interval_unit']) && empty($form_state['values']['interval_length'])) {
    form_set_error('interval_length', t('Interval length must be set if interval unit is set.'));
  }
  if ($form_state['values']['gc_type'] == 'S' && empty($form_state['values']['interval_unit'])) {
    form_set_error('interval_unit', t('Interval unit must be set if type is Subscription.'));
  }
  if ($form_state['values']['gc_type'] == 'S' && empty($form_state['values']['interval_length'])) {
    form_set_error('interval_length', t('Interval length must be set if type is Subscription.'));
  }
}

/**
 * Implements hook_node_form_submit().
 *
 *  Updates gc_client_products table.
 */
function uc_gc_client_node_form_submit($form, &$form_state) {
  if (uc_product_is_product_form($form) && isset($form_state['values']['nid'])) {
    $nid = $form_state['values']['nid'];
    db_merge('uc_gc_client_products')
      ->key(array('nid' => $nid))
      ->fields(array(
        'nid' => $nid,
        'type' => !empty($form_state['values']['gc_type']) ? $form_state['values']['gc_type'] : NULL,
        'price_x' => $form_state['values']['price_x'],
        'start_date' => !is_null($form_state['values']['start_date']) ? strtotime($form_state['values']['start_date']) : NULL,
        'interval_length' => !empty($form_state['values']['interval_length']) ? $form_state['values']['interval_length'] : NULL,
        'interval_unit' => !empty($form_state['values']['interval_unit']) ? $form_state['values']['interval_unit'] : NULL,
        'raise_payment' => !empty($form_state['values']['raise_payment']) ? 1 : 0,
      ))
      ->execute();
  }
}

/**
 * Implements hook_node_delete().
 *
 * Delete row from {gc_client_products} table.
 */
function uc_gc_client_node_delete($node) {
  if (uc_product_is_product($node)) {
    db_delete('uc_gc_client_products')
      ->condition('nid', $node->nid, '=')
      ->execute();
  }
}

/**
 * Implements hook_field_extra_fields().
 *
 * Adds the "Product information".
 */
function uc_gc_client_field_extra_fields() {
  $extra = array();
  foreach (uc_product_types() as $type) {
    $extra['node'][$type] = array(
      'display' => array(
        'type' => array(
          'label' => t('GoCardless type'),
          'description' => t('Subscription or pre-authorization'),
          'weight' => 0,
        ),
        'start_date' => array(
          'label' => t('Start date for new mandate'),
          'description' => t('Start date for new mandate description'),
          'weight' => 2,
        ),
      ),
    );
  }
  return $extra;
}

/**
 * Implements hook_node_view().
 */
function uc_gc_client_node_view($node, $view_mode, $langcode) {
  if (uc_product_is_product($node)) {
    $nid = $node->nid;
    $extras = db_select('uc_gc_client_products', 'p')
      ->fields('p')
      ->condition('nid', $nid, '=')
      ->execute()
      ->fetchAssoc();

    if ($extras['start_date']) {
      $start_date = format_date($extras['start_date'], 'uc_store');
      $node->content['start_date'] = array(
        '#markup' => '<p>' . t('Your first direct debit payment will be paid on') . '<b>' . $start_date . '</b>.</p>',
        '#value' => $start_date,
      );
    }
  }
}

/**
 * Implements hook_form().
 *
 * Provides an interface to enable / disable GC countries.
 */
function uc_gc_client_countries_form($form, &$form_state) {
  $uc_countries = db_select('uc_countries', 'u')
    ->fields('u')
    ->condition('version', 0, '>')
    ->execute()->fetchAllAssoc('country_iso_code_2');

  $gc_countries = variable_get('uc_gc_client_countries');
  $countries = array();
  $default_value = array();
  foreach ($gc_countries as $gc_code => $gc_country) {
    if (isset($uc_countries[$gc_code])) {
      $country_name = $uc_countries[$gc_code]->country_name;
      $countries_[$country_name] = $gc_code;
    }
  }
  ksort($countries_);
  foreach ($countries_ as $name => $gc_code) {
    $countries[$gc_code] = array(
      $name,
      $gc_countries[$gc_code]['region'],
      $gc_countries[$gc_code]['sign'],
    );
    if ($gc_countries[$gc_code]['enabled']) {
      $default_value[$gc_code] = TRUE;
    }
  }
  $form = array();
  $form['markup'] = array(
    '#markup' => '<p>' .  t('Countries should first be enabled in Ubercart before they can be enabled / disabled here.') . '</p>',
  );
  $header = array('Country', 'Direct debit scheme', 'Currency');
  $form['countries'] = array(
    '#type' => 'tableselect',
    '#header' => $header,
    '#options' => $countries,
    '#empty' => t('No countries available.'),
    '#default_value' => $default_value,
  );
  $form['submit'] = array(
    '#type' => 'submit',
    '#value' => t('Enable / disable countries'),
  );
  return $form;
}

/**
 * Implements hook_form_submit().
 */
function uc_gc_client_countries_form_submit($form, &$form_state) {
  $countries = $form_state['values']['countries'];
  $gc_countries = variable_get('uc_gc_client_countries');
  foreach ($countries as $code => $country) {
    if ($country) {
      $gc_countries[$code]['enabled'] = 1;
    }
    else {
      $gc_countries[$code]['enabled'] = 0;
    }
  }
  variable_set('uc_gc_client_countries', $gc_countries);
}
